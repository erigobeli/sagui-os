/* arquivos.c - arquivos abertos e as chamadas de arquivos e pastas (H14).
 *
 * Cada processo tem uma tabela de 16 descritores; cada descritor aponta
 * para um "arquivo aberto", que guarda o que é (um nó do VFS, o terminal ou
 * uma ponta de pipe), o modo e a posição. Vários descritores (de um ou de
 * vários processos) podem apontar para o mesmo arquivo aberto, depois de um
 * duplicar ou de um executar; ele conta as referências e só é fechado de
 * verdade quando a última some. */
#include "abi.h"
#include "console.h"
#include "caminho.h"
#include "memoria.h"
#include "pipe.h"
#include "processo.h"
#include "sistema.h"
#include "terminal_es.h"
#include "texto.h"
#include "vfs.h"
#include "fat32.h"
#include "rede.h"

enum { A_NO = 1, A_TERMINAL, A_PIPE_LER, A_PIPE_ESCREVER, A_FAT, A_TCP };

typedef struct arquivo {
    int      tipo;
    int      modo;
    uint32_t pos;
    no_t    *no;
    pipe_t  *pipe;
    fat_arq_t *fat;      /* arquivo no pendrive (/usb) */
    struct tcp_conexao *tcp;   /* conexão de rede (D032) */
    int      refs;
} arquivo_t;

/* Um arquivo do pendrive só vale enquanto a mesma montagem continuar. */
static bool fat_vivo(arquivo_t *a) { return a->fat && a->fat->geracao == fat_geracao; }

static arquivo_t *arquivo_novo(int tipo, int modo)
{
    arquivo_t *a = kcalloc(sizeof(arquivo_t));
    if (a) { a->tipo = tipo; a->modo = modo; a->refs = 1; }
    return a;
}

static void arquivo_soltar(arquivo_t *a)
{
    if (--a->refs > 0) return;
    if (a->tipo == A_NO) vfs_soltar(a->no);
    else if (a->tipo == A_FAT) { if (fat_vivo(a)) fat_fechar(a->fat); kfree(a->fat); }
    else if (a->tipo == A_TCP) tcp_fechar(a->tcp);
    else if (a->tipo == A_PIPE_LER) pipe_fechar_ponta(a->pipe, false);
    else if (a->tipo == A_PIPE_ESCREVER) pipe_fechar_ponta(a->pipe, true);
    kfree(a);
}

static int fd_livre(processo_t *p)
{
    for (int i = 0; i < MAX_ARQUIVOS; i++)
        if (!p->arquivos[i]) return i;
    return ERRO_LIMITE;
}

static arquivo_t *pegar(uint32_t fd)
{
    return fd < MAX_ARQUIVOS ? atual->arquivos[fd] : NULL;
}

/* O filho herda só a entrada, a saída e a saída de erros (D010). O
 * primeiro programa, sem pai, recebe o terminal nas três. */
void arquivos_herdar(processo_t *filho, processo_t *pai)
{
    if (!pai) {
        arquivo_t *t = arquivo_novo(A_TERMINAL, ABRIR_LER | ABRIR_ESCREVER);
        if (!t) return;
        t->refs = 3;
        for (int i = 0; i < 3; i++) filho->arquivos[i] = t;
        return;
    }
    for (int i = 0; i < 3; i++)
        if (pai->arquivos[i]) {
            filho->arquivos[i] = pai->arquivos[i];
            pai->arquivos[i]->refs++;
        }
}

void arquivos_fechar_todos(processo_t *p)
{
    for (int i = 0; i < MAX_ARQUIVOS; i++)
        if (p->arquivos[i]) {
            arquivo_soltar(p->arquivos[i]);
            p->arquivos[i] = NULL;
        }
}

/* Texto do programa -> caminho absoluto simplificado. */
static int caminho_do_usuario(uint32_t ptr, char *saida)
{
    char bruto[CAMINHO_MAX];
    int r = usuario_texto((const char *)ptr, bruto, sizeof bruto);
    if (r < 0) return r;
    if (r == 0) return ERRO_INVALIDO;
    if (!caminho_resolver(atual->pasta, bruto, saida, CAMINHO_MAX)) return ERRO_INVALIDO;
    return 0;
}

int32_t sis_abrir(uint32_t ptr, uint32_t modo, uint32_t c, uint32_t d, uint32_t e)
{
    (void)c; (void)d; (void)e;
    char caminho[CAMINHO_MAX];
    int r = caminho_do_usuario(ptr, caminho);
    if (r < 0) return r;
    if (!(modo & (ABRIR_LER | ABRIR_ESCREVER))) modo |= ABRIR_LER;
    if (modo & (ABRIR_CRIAR | ABRIR_TRUNCAR | ABRIR_ACRESCENTAR)) modo |= ABRIR_ESCREVER;
    const char *rel;
    if (vfs_na_usb(caminho, &rel)) {
        if (!*rel) return ERRO_E_PASTA;
        int fd = fd_livre(atual);
        if (fd < 0) return fd;
        arquivo_t *a = arquivo_novo(A_FAT, (int)modo);
        fat_arq_t *f = kmalloc(sizeof(fat_arq_t));
        if (!a || !f) { if (a) kfree(a); if (f) kfree(f); return ERRO_SEM_MEMORIA; }
        r = fat_abrir(rel, (int)modo, f);
        if (r < 0) { kfree(a); kfree(f); return r; }
        a->fat = f;
        atual->arquivos[fd] = a;
        return fd;
    }
    no_t *x = vfs_achar(caminho);
    if (!x) {
        if (!(modo & ABRIR_CRIAR)) return ERRO_NAO_EXISTE;
        r = vfs_criar(caminho, TIPO_ARQUIVO, &x);
        if (r < 0) return r;
    }
    if (x->tipo == TIPO_PASTA) return ERRO_E_PASTA;
    int fd = fd_livre(atual);
    if (fd < 0) return fd;
    arquivo_t *a = arquivo_novo(A_NO, (int)modo);
    if (!a) return ERRO_SEM_MEMORIA;
    if (modo & ABRIR_TRUNCAR) vfs_truncar(x);
    a->no = x;
    vfs_segurar(x);
    atual->arquivos[fd] = a;
    return fd;
}

int32_t sis_ler(uint32_t fd, uint32_t buf, uint32_t n, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    arquivo_t *a = pegar(fd);
    if (!a) return ERRO_DESCRITOR;
    if (!usuario_ok((void *)buf, n, true)) return ERRO_ENDERECO;
    switch (a->tipo) {
    case A_NO: {
        if (!(a->modo & ABRIR_LER)) return ERRO_ACESSO;
        uint32_t r = vfs_ler(a->no, a->pos, (void *)buf, n);
        a->pos += r;
        return (int32_t)r;
    }
    case A_FAT: {
        if (!(a->modo & ABRIR_LER)) return ERRO_ACESSO;
        if (!fat_vivo(a)) return ERRO_NAO_EXISTE;   /* pendrive desmontado ou removido */
        int r = fat_ler(a->fat, a->pos, (void *)buf, n);
        if (r > 0) a->pos += (uint32_t)r;
        return r;
    }
    case A_TERMINAL: {
        int32_t r = terminal_ler((char *)buf, n);
        console_mostrar();   /* o eco do que foi digitado aparece já */
        return r;
    }
    case A_PIPE_LER:  return pipe_ler(a->pipe, (char *)buf, n);
    case A_TCP:       return tcp_ler(a->tcp, (char *)buf, n);
    default:          return ERRO_ACESSO;
    }
}

int32_t sis_escrever(uint32_t fd, uint32_t buf, uint32_t n, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    arquivo_t *a = pegar(fd);
    if (!a) return ERRO_DESCRITOR;
    if (!usuario_ok((void *)buf, n, false)) return ERRO_ENDERECO;
    switch (a->tipo) {
    case A_NO: {
        if (!(a->modo & ABRIR_ESCREVER)) return ERRO_ACESSO;
        if (a->modo & ABRIR_ACRESCENTAR) a->pos = a->no->tamanho;
        int r = vfs_escrever(a->no, a->pos, (const void *)buf, n);
        if (r > 0) a->pos += (uint32_t)r;
        return r;
    }
    case A_FAT: {
        if (!(a->modo & ABRIR_ESCREVER)) return ERRO_ACESSO;
        if (!fat_vivo(a)) return ERRO_NAO_EXISTE;
        if (a->modo & ABRIR_ACRESCENTAR) a->pos = a->fat->tamanho;
        int r = fat_escrever(a->fat, a->pos, (const void *)buf, n);
        if (r > 0) a->pos += (uint32_t)r;
        return r;
    }
    case A_TERMINAL:      return terminal_escrever((const char *)buf, n);
    case A_PIPE_ESCREVER: return pipe_escrever(a->pipe, (const char *)buf, n);
    case A_TCP:           return tcp_escrever(a->tcp, (const char *)buf, n);
    default:              return ERRO_ACESSO;
    }
}

int32_t sis_fechar(uint32_t fd, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    arquivo_t *a = pegar(fd);
    if (!a) return ERRO_DESCRITOR;
    atual->arquivos[fd] = NULL;
    arquivo_soltar(a);
    return 0;
}

int32_t sis_posicionar(uint32_t fd, uint32_t desloc, uint32_t origem, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    arquivo_t *a = pegar(fd);
    if (!a) return ERRO_DESCRITOR;
    if (a->tipo != A_NO && a->tipo != A_FAT) return ERRO_ACESSO;
    uint32_t tam = a->tipo == A_NO ? a->no->tamanho : a->fat->tamanho;
    int32_t base = origem == POS_INICIO ? 0 : origem == POS_ATUAL ? (int32_t)a->pos :
                   origem == POS_FIM ? (int32_t)tam : -1;
    if (base < 0) return ERRO_INVALIDO;
    int32_t nova = base + (int32_t)desloc;
    if (nova < 0) return ERRO_INVALIDO;
    a->pos = (uint32_t)nova;
    return nova;
}

int32_t sis_info_arquivo(uint32_t ptr, uint32_t pinfo, uint32_t c, uint32_t d, uint32_t e)
{
    (void)c; (void)d; (void)e;
    char caminho[CAMINHO_MAX];
    int r = caminho_do_usuario(ptr, caminho);
    if (r < 0) return r;
    info_arquivo_t *info = (info_arquivo_t *)pinfo;
    if (!usuario_ok(info, sizeof *info, true)) return ERRO_ENDERECO;
    const char *rel;
    if (vfs_na_usb(caminho, &rel)) {
        if (!*rel) { info->tipo = TIPO_PASTA; info->tamanho = 0; return 0; }
        return fat_info(rel, info);
    }
    no_t *x = vfs_achar(caminho);
    if (!x) return ERRO_NAO_EXISTE;
    info->tipo = x->tipo;
    info->tamanho = x->tamanho;
    return 0;
}

int32_t sis_criar_pasta(uint32_t ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    char caminho[CAMINHO_MAX];
    int r = caminho_do_usuario(ptr, caminho);
    if (r < 0) return r;
    const char *rel;
    if (vfs_na_usb(caminho, &rel)) return *rel ? fat_criar_pasta(rel) : ERRO_JA_EXISTE;
    return vfs_criar(caminho, TIPO_PASTA, NULL);
}

int32_t sis_remover_pasta(uint32_t ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    char caminho[CAMINHO_MAX];
    int r = caminho_do_usuario(ptr, caminho);
    if (r < 0) return r;
    /* Não deixa remover a pasta em que algum processo está. */
    for (int i = 0; i < MAX_PROCESSOS; i++) {
        processo_t *p = processo_indice(i);
        if (p->estado != P_LIVRE && p->estado != P_ZUMBI && txt_compara(p->pasta, caminho) == 0)
            return ERRO_OCUPADO;
    }
    const char *rel;
    if (vfs_na_usb(caminho, &rel)) return *rel ? fat_remover_pasta(rel) : ERRO_ACESSO;
    return vfs_remover_pasta(caminho);
}

int32_t sis_apagar(uint32_t ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    char caminho[CAMINHO_MAX];
    int r = caminho_do_usuario(ptr, caminho);
    if (r < 0) return r;
    const char *rel;
    if (vfs_na_usb(caminho, &rel)) return *rel ? fat_apagar(rel) : ERRO_E_PASTA;
    return vfs_apagar(caminho);
}

int32_t sis_listar(uint32_t ptr, uint32_t indice, uint32_t pent, uint32_t d, uint32_t e)
{
    (void)d; (void)e;
    char caminho[CAMINHO_MAX];
    int r = caminho_do_usuario(ptr, caminho);
    if (r < 0) return r;
    if (!usuario_ok((void *)pent, sizeof(entrada_pasta_t), true)) return ERRO_ENDERECO;
    const char *rel;
    if (vfs_na_usb(caminho, &rel)) return fat_listar(rel, (int)indice, (entrada_pasta_t *)pent);
    return vfs_listar(caminho, (int)indice, (entrada_pasta_t *)pent);
}

int32_t sis_mudar_pasta(uint32_t ptr, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    char caminho[CAMINHO_MAX];
    int r = caminho_do_usuario(ptr, caminho);
    if (r < 0) return r;
    const char *rel;
    if (vfs_na_usb(caminho, &rel)) {
        if (*rel) {
            info_arquivo_t inf;
            r = fat_info(rel, &inf);
            if (r < 0) return r;
            if (inf.tipo != TIPO_PASTA) return ERRO_NAO_E_PASTA;
        }
        txt_copia_n(atual->pasta, caminho, sizeof atual->pasta);
        return 0;
    }
    no_t *x = vfs_achar(caminho);
    if (!x) return ERRO_NAO_EXISTE;
    if (x->tipo != TIPO_PASTA) return ERRO_NAO_E_PASTA;
    txt_copia_n(atual->pasta, caminho, sizeof atual->pasta);
    return 0;
}

int32_t sis_pasta_atual(uint32_t buf, uint32_t n, uint32_t c, uint32_t d, uint32_t e)
{
    (void)c; (void)d; (void)e;
    uint32_t precisa = txt_tamanho(atual->pasta) + 1;
    if (n < precisa) return ERRO_LIMITE;
    if (!usuario_ok((void *)buf, precisa, true)) return ERRO_ENDERECO;
    memcpy((void *)buf, atual->pasta, precisa);
    return (int32_t)precisa - 1;
}

int32_t sis_renomear(uint32_t po, uint32_t pd, uint32_t c, uint32_t d, uint32_t e)
{
    (void)c; (void)d; (void)e;
    char origem[CAMINHO_MAX], destino[CAMINHO_MAX];
    int r = caminho_do_usuario(po, origem);
    if (r < 0) return r;
    r = caminho_do_usuario(pd, destino);
    if (r < 0) return r;
    const char *ro, *rd;
    bool o_usb = vfs_na_usb(origem, &ro), d_usb = vfs_na_usb(destino, &rd);
    if (o_usb && d_usb) return fat_renomear(ro, rd);
    if (o_usb || d_usb) return ERRO_ACESSO;   /* entre a RAM e o pendrive: o mv copia e apaga */
    return vfs_renomear(origem, destino);
}

int32_t sis_pipe(uint32_t pfds, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    int *fds = (int *)pfds;
    if (!usuario_ok(fds, 2 * sizeof(int), true)) return ERRO_ENDERECO;
    int fl = fd_livre(atual);
    if (fl < 0) return fl;
    atual->arquivos[fl] = (arquivo_t *)1;   /* reserva enquanto procura o segundo */
    int fe = fd_livre(atual);
    atual->arquivos[fl] = NULL;
    if (fe < 0) return fe;
    pipe_t *p = pipe_criar();
    arquivo_t *al = arquivo_novo(A_PIPE_LER, ABRIR_LER);
    arquivo_t *ae = arquivo_novo(A_PIPE_ESCREVER, ABRIR_ESCREVER);
    if (!p || !al || !ae) {
        if (p) kfree(p);
        if (al) kfree(al);
        if (ae) kfree(ae);
        return ERRO_SEM_MEMORIA;
    }
    al->pipe = ae->pipe = p;
    atual->arquivos[fl] = al;
    atual->arquivos[fe] = ae;
    fds[0] = fl;
    fds[1] = fe;
    return 0;
}

/* duplicar(antigo, novo): o descritor "novo" passa a apontar para o mesmo
 * arquivo aberto que "antigo" (fechando o que havia nele). É assim que o
 * shell liga a saída de um programa a um arquivo ou a um pipe. */
int32_t sis_duplicar(uint32_t antigo, uint32_t novo, uint32_t c, uint32_t d, uint32_t e)
{
    (void)c; (void)d; (void)e;
    arquivo_t *a = pegar(antigo);
    if (!a || novo >= MAX_ARQUIVOS) return ERRO_DESCRITOR;
    if (antigo == novo) return (int32_t)novo;
    a->refs++;
    if (atual->arquivos[novo]) arquivo_soltar(atual->arquivos[novo]);
    atual->arquivos[novo] = a;
    return (int32_t)novo;
}

/* disponivel(fd): quanto dá para ler de um pipe sem bloquear. O Terminal em
 * janela usa isto para olhar a saída do shell e os eventos da janela no
 * mesmo laço, sem ficar preso em nenhum dos dois. */
int arquivos_novo_tcp(struct tcp_conexao *c)
{
    int fd = fd_livre(atual);
    if (fd < 0) return fd;
    arquivo_t *a = arquivo_novo(A_TCP, ABRIR_LER | ABRIR_ESCREVER);
    if (!a) return ERRO_SEM_MEMORIA;
    a->tcp = c;
    atual->arquivos[fd] = a;
    return fd;
}

int32_t sis_disponivel(uint32_t fd, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    arquivo_t *a = pegar(fd);
    if (!a) return ERRO_DESCRITOR;
    if (a->tipo != A_PIPE_LER) return ERRO_INVALIDO;
    if (a->pipe->qtd) return (int32_t)a->pipe->qtd;
    return a->pipe->escritores ? 0 : ERRO_PIPE_QUEBRADO;
}

int32_t sis_modo_terminal(uint32_t modo, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)b; (void)c; (void)d; (void)e;
    if (modo != TERM_LINHA && modo != TERM_BRUTO) return ERRO_INVALIDO;
    terminal_modo((int)modo);
    return 0;
}

int32_t sis_limpar_tela(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    (void)a; (void)b; (void)c; (void)d; (void)e;
    terminal_limpar();
    return 0;
}
