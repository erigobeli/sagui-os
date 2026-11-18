/* autoteste.c - testes que o kernel executa sobre si mesmo.
 *
 * A imagem normal nunca roda nada daqui. Os testes automáticos (tests/)
 * geram uma imagem com o nome de um teste gravado no cabeçalho de boot
 * (tools/mkimagem.py --teste NOME); o kernel o recebe na boot_info e chama
 * autoteste_executar(). O resultado vai para a serial, que o teste lê:
 * "[TESTE] <nome> ok" ou "[TESTE] <nome> FALHOU: motivo". */
#include "autoteste.h"
#include "bootinfo.h"
#include "console.h"
#include "cpu.h"
#include "relogio.h"
#include "texto.h"
#include "teclado.h"
#include "sgfs.h"
#include "caminho.h"
#include "paginacao.h"
#include "processo.h"
#include "abi.h"
#include "vfs.h"
#include "video.h"
#include "disco.h"
#include "memoria.h"

static int falhas;

static void confere(bool cond, const char *o_que)
{
    if (!cond) {
        falhas++;
        klog("[TESTE] falha: %s\n", o_que);
    }
}

/* H05: o kernel provoca uma divisão por zero; a tela de erro tem que aparecer. */
static void teste_divisao(void)
{
    kprintf("Autoteste: vou dividir por zero de propósito...\n");
    volatile int zero = 0;
    volatile int r = 10 / zero;
    (void)r;
    klog("[TESTE] divisao FALHOU: a CPU nao gerou a excecao\n");
}

/* H05: o contador de tempo avança, e na velocidade certa (comparado ao CMOS). */
static void teste_relogio(void)
{
    uint32_t t0 = relogio_tiques();
    data_hora_t d0, d1;
    cmos_ler_data(&d0);
    for (int i = 1; i <= 3; i++) {
        relogio_dormir_ms(1000);
        klog("[TESTE] segundos desde o boot: %u (tiques %u)\n", relogio_segundos(), relogio_tiques());
    }
    cmos_ler_data(&d1);
    uint32_t dt = relogio_tiques() - t0;
    confere(dt >= 300 && dt <= 310, "3 segundos devem dar ~300 tiques");
    int s0 = d0.minuto * 60 + d0.segundo, s1 = d1.minuto * 60 + d1.segundo;
    int passou = (s1 - s0 + 3600) % 3600;
    klog("[TESTE] CMOS andou %d s enquanto o PIT contou %u tiques\n", passou, dt);
    confere(passou >= 2 && passou <= 4, "o relógio CMOS deve andar ~3 s");
}

/* Gerador pseudoaleatório simples (congruencial linear), só para variar os
 * tamanhos do teste de forma reprodutível. */
static uint32_t semente = 12345;
static uint32_t aleatorio(void)
{
    semente = semente * 1103515245u + 12345u;
    return semente >> 8;
}

#define BLOCOS 300

static uint8_t padrao(uint32_t bloco, uint32_t i) { return (uint8_t)(bloco * 31 + i * 7 + 1); }

static void preencher(uint8_t *p, uint32_t tam, uint32_t id)
{
    for (uint32_t i = 0; i < tam; i++) p[i] = padrao(id, i);
}

static bool intacto(const uint8_t *p, uint32_t tam, uint32_t id)
{
    for (uint32_t i = 0; i < tam; i++)
        if (p[i] != padrao(id, i)) return false;
    return true;
}

/* H06: páginas físicas e kmalloc/kfree. */
static void teste_memoria(void)
{
    extern char _kernel_inicio[], _kernel_fim[];
    static uint32_t paginas[1000];
    static uint8_t *ptr[BLOCOS];
    static uint32_t tam[BLOCOS], id[BLOCOS];
    estat_memoria_t e0, e1;
    memoria_estatisticas(&e0);
    klog("[TESTE] livres no inicio: %u paginas\n", e0.paginas_livres);

    /* 1. Páginas avulsas: alinhadas, distintas, fora do kernel e do 1º MB. */
    bool ok = true;
    for (int i = 0; i < 1000; i++) {
        paginas[i] = memfisica_alocar();
        uint32_t p = paginas[i];
        if (!p || (p & 0xFFF) || p < 0x100000 || p >= MEMFIS_LIMITE ||
            (p >= (uint32_t)(uintptr_t)_kernel_inicio && p < (uint32_t)(uintptr_t)_kernel_fim) ||
            (p >= info_boot.ramdisk_inicio && p < info_boot.ramdisk_inicio + info_boot.ramdisk_bytes))
            ok = false;
        *(volatile uint32_t *)(uintptr_t)p = 0xCAFE0000u + i;   /* a página tem que ser RAM de verdade */
    }
    for (int i = 0; i < 1000; i++)
        if (*(volatile uint32_t *)(uintptr_t)paginas[i] != 0xCAFE0000u + i) ok = false;
    confere(ok, "1000 páginas válidas, distintas e graváveis");
    for (int i = 0; i < 1000; i++) memfisica_liberar(paginas[i]);
    confere(memfisica_livres() == e0.paginas_livres, "páginas avulsas voltam ao bitmap");

    uint32_t seq = memfisica_alocar_seguidas(16);
    confere(seq != 0 && (seq & 0xFFF) == 0, "16 páginas seguidas");
    memset((void *)(uintptr_t)seq, 0x5A, 16 * PAGINA);
    memfisica_liberar_seguidas(seq, 16);

    /* 2. kmalloc com tamanhos variados (pequenos e grandes). */
    for (uint32_t i = 0; i < BLOCOS; i++) {
        uint32_t r = aleatorio();
        tam[i] = (r & 3) == 0 ? 2100 + r % 40000 : 1 + r % 2040;
        ptr[i] = kmalloc(tam[i]);
        id[i] = i;
        confere(ptr[i] != NULL, "kmalloc devolveu memória");
        if (!ptr[i]) return;
        confere(((uint32_t)(uintptr_t)ptr[i] & 7) == 0, "ponteiro alinhado em 8 bytes");
        preencher(ptr[i], tam[i], id[i]);
    }
    /* Nenhum bloco pode se sobrepor a outro. */
    ok = true;
    for (uint32_t i = 0; i < BLOCOS; i++)
        for (uint32_t j = i + 1; j < BLOCOS; j++)
            if ((uint32_t)(uintptr_t)ptr[i] < (uint32_t)(uintptr_t)ptr[j] + tam[j] && (uint32_t)(uintptr_t)ptr[j] < (uint32_t)(uintptr_t)ptr[i] + tam[i])
                ok = false;
    confere(ok, "nenhum bloco se sobrepõe a outro");

    /* Libera metade, aloca de novo em cima dos buracos, confere tudo. */
    for (uint32_t i = 0; i < BLOCOS; i += 2) kfree(ptr[i]);
    for (uint32_t i = 0; i < BLOCOS; i += 2) {
        uint32_t r = aleatorio();
        tam[i] = (r & 7) == 0 ? 3000 + r % 20000 : 1 + r % 1500;
        id[i] = 1000 + i;
        ptr[i] = kmalloc(tam[i]);
        if (!ptr[i]) { confere(false, "realocação"); return; }
        preencher(ptr[i], tam[i], id[i]);
    }
    ok = true;
    for (uint32_t i = 0; i < BLOCOS; i++)
        if (!intacto(ptr[i], tam[i], id[i])) ok = false;
    confere(ok, "todos os blocos mantêm o conteúdo depois de liberar e realocar");

    /* Liberar duas vezes é detectado e não estraga nada. */
    uint8_t *x = kmalloc(40);
    kfree(x);
    kfree(x);
    uint8_t *y = kmalloc(40), *z = kmalloc(40);
    confere(y != z, "liberação dupla não faz o mesmo bloco ser entregue duas vezes");
    kfree(y); kfree(z);

    for (uint32_t i = 0; i < BLOCOS; i++) kfree(ptr[i]);
    memoria_estatisticas(&e1);
    klog("[TESTE] livres no fim: %u paginas; paginas nas classes: %u -> %u; blocos em uso %u; grandes %u\n",
         e1.paginas_livres, e0.heap_paginas, e1.heap_paginas, e1.heap_blocos_usados, e1.heap_grandes);
    confere(e1.heap_blocos_usados == e0.heap_blocos_usados && e1.heap_grandes == e0.heap_grandes,
            "nenhum bloco ficou marcado como em uso");
    confere(e1.paginas_livres + (e1.heap_paginas - e0.heap_paginas) == e0.paginas_livres,
            "páginas grandes voltaram ao bitmap (só as das classes pequenas ficam com o heap)");
    kprintf("Autoteste de memória: %u blocos de 1 B a 42 KB alocados, conferidos e liberados.\n", BLOCOS * 3 / 2);
}

/* H07: ecoa o que for digitado até o Enter e registra os códigos na serial,
 * para o teste comparar com o que deveria sair do mapa ABNT2. */
static void teste_teclado(void)
{
    char hex[400];
    int n = 0;
    kprintf("Autoteste do teclado: digite e aperte Enter.\n> ");
    klog("[TESTE] teclado pronto\n");
    for (;;) {
        int t = teclado_ler();
        if (t == '\n') break;
        klog("[TECLA] %x\n", t);
        if (t >= 0x20 && t < 0x100 && t != 0x7F) kprintf("%c", t);
        else if (t < 0x20) kprintf("^%c", t + '@');
        if (n < (int)sizeof hex - 5) n += snformatar(hex + n, sizeof hex - n, "%02x", t & 0xFF);
    }
    kprintf("\n");
    klog("[TESTE] teclado linha=%s\n", hex);
}

/* H08: lista todas as entradas e lê o leia-me.txt inteiro, em pedaços. */
static void listar_sgfs(const char *pasta, int nivel)
{
    uint32_t pos = 0;
    const sgfs_entrada_t *e;
    while ((e = sgfs_proximo_filho(pasta, &pos))) {
        static const char *tipos[] = { "?", "arquivo", "pasta", "programa" };
        klog("[SGFS] %-8s %6u /%s\n", tipos[e->tipo <= 3 ? e->tipo : 0], e->tamanho, e->caminho);
        kprintf("%*s%s%s\n", nivel * 2 + 2, "", sgfs_nome(e), e->tipo == SGFS_PASTA ? "/" : "");
        if (e->tipo == SGFS_PASTA)
            listar_sgfs(e->caminho, nivel + 1);
    }
}

static bool caminho_da(const char *atual, const char *alvo, const char *esperado)
{
    char saida[CAMINHO_MAX];
    bool ok = caminho_resolver(atual, alvo, saida, sizeof saida) && txt_compara(saida, esperado) == 0;
    if (!ok) klog("[TESTE] caminho %s + %s deu %s (esperado %s)\n", atual, alvo, saida, esperado);
    return ok;
}

static void teste_sgfs(void)
{
    kprintf("Conteúdo do ramdisk:\n");
    listar_sgfs("/", 0);
    uint32_t n = 0;
    for (uint32_t i = 0; i < sgfs_quantidade(); i++) n++;
    klog("[SGFS] total %u entradas\n", n);

    const sgfs_entrada_t *e = sgfs_procurar("/leia-me.txt");
    confere(e && e->tipo == SGFS_ARQUIVO, "leia-me.txt existe e é arquivo");
    if (!e) return;
    char *buf = kmalloc(e->tamanho + 1);
    uint32_t lidos = 0, r;
    while ((r = sgfs_ler(e, lidos, buf + lidos, 100)) > 0)   /* pedaços de 100: testa o deslocamento */
        lidos += r;
    confere(lidos == e->tamanho, "leu o arquivo inteiro");
    klog("[ARQ-INICIO]\n");
    for (uint32_t i = 0; i < lidos; i++) serial_char(buf[i]);
    klog("[ARQ-FIM]\n");
    kfree(buf);

    confere(sgfs_procurar("/nao-existe.txt") == NULL, "arquivo inexistente não é encontrado");
    confere(sgfs_procurar("/historia") && sgfs_procurar("historia")->tipo == SGFS_PASTA, "historia é pasta");
    confere(sgfs_procurar("/")->tipo == SGFS_PASTA, "a raiz é pasta");
    char lixo[4];
    confere(sgfs_ler(sgfs_procurar("/historia"), 0, lixo, 4) == 0, "ler uma pasta não devolve dados");
    confere(sgfs_ler(e, e->tamanho, lixo, 4) == 0, "ler depois do fim devolve 0");
    confere(caminho_da("/historia", "../arte/./sagui.txt", "/arte/sagui.txt"), "caminho com .. e .");
    confere(caminho_da("/", "..", "/"), ".. na raiz continua na raiz");
    confere(caminho_da("/a/b", "/c//d/", "/c/d"), "caminho absoluto com barras repetidas");
    confere(caminho_da("/a", "b/../../..", "/"), "vários .. seguidos");
}

/* H10: dois espaços de endereço, mesmo endereço virtual, valores diferentes. */
static uint32_t valor_global = 0x5A6E1234;

static void teste_paginacao(void)
{
    uint32_t livres0 = memfisica_livres();
    espaco_t e1 = espaco_criar(), e2 = espaco_criar();
    confere(e1 && e2 && e1 != e2, "dois diretórios criados");
    const uint32_t v = USUARIO_INICIO;
    confere(espaco_alocar(e1, v, PAGINA, PG_ESCRITA | PG_USUARIO), "página no espaço 1");
    confere(espaco_alocar(e2, v, PAGINA, PG_ESCRITA | PG_USUARIO), "página no espaço 2");
    confere(espaco_traduzir(espaco_kernel(), v, NULL) == 0, "o espaço do kernel não vê a área dos programas");

    espaco_ativar(e1);
    *(volatile uint32_t *)(uintptr_t)v = 0x11111111;
    confere(valor_global == 0x5A6E1234, "o kernel continua visível dentro do espaço 1");
    espaco_ativar(e2);
    *(volatile uint32_t *)(uintptr_t)v = 0x22222222;
    espaco_ativar(e1);
    uint32_t lido1 = *(volatile uint32_t *)(uintptr_t)v;
    espaco_ativar(e2);
    uint32_t lido2 = *(volatile uint32_t *)(uintptr_t)v;
    espaco_ativar(espaco_kernel());
    klog("[TESTE] endereco virtual %x: espaco 1 = %x (fisico %x), espaco 2 = %x (fisico %x)\n",
         v, lido1, espaco_traduzir(e1, v, NULL), lido2, espaco_traduzir(e2, v, NULL));
    kprintf("Mesmo endereço 0x%08x: espaço 1 lê 0x%08x, espaço 2 lê 0x%08x\n", v, lido1, lido2);
    confere(lido1 == 0x11111111 && lido2 == 0x22222222, "cada espaço lê o próprio valor");
    confere(espaco_traduzir(e1, v, NULL) != espaco_traduzir(e2, v, NULL), "páginas físicas diferentes");

    espaco_destruir(e1);
    espaco_destruir(e2);
    confere(memfisica_livres() == livres0, "destruir os espaços devolve todas as páginas");
}

/* H10: acesso a um endereço não mapeado tem que gerar a tela de falta de página. */
static void teste_falta_pagina(void)
{
    kprintf("Autoteste: vou ler o endereço 0x90000000, que não está mapeado...\n");
    volatile uint32_t x = *(volatile uint32_t *)0x90000000;
    (void)x;
    klog("[TESTE] falta_pagina FALHOU: a leitura passou\n");
}

/* H11: duas tarefas que só escrevem, sem nunca ceder a vez. Se a saída
 * alternar entre A e B, quem alternou foi o escalonador, no tique do relógio. */
static volatile int tarefas_vivas;

static void tarefa_letra(void *arg)
{
    char letra = (char)(uint32_t)(uintptr_t)arg;
    for (int i = 0; i < 60; i++) {
        serial_char(letra);
        console_char(letra);
        /* Espera ativa até o próximo tique: gasta CPU sem chamar o escalonador. */
        uint32_t t = relogio_tiques();
        while (relogio_tiques() == t)
            ;
    }
    tarefas_vivas--;
}

static void teste_tarefas(void)
{
    kprintf("Duas tarefas imprimindo A e B, sem cooperar:\n");
    tarefas_vivas = 2;
    klog("[TESTE] saida=");
    processo_t *a = tarefa_kernel("tarefa-A", tarefa_letra, (void *)'A');
    processo_t *b = tarefa_kernel("tarefa-B", tarefa_letra, (void *)'B');
    confere(a && b && a->pid != b->pid, "duas tarefas com PIDs diferentes");
    while (tarefas_vivas > 0)
        cpu_espera();   /* este é o processo 0: só roda quando A e B não estão prontos */
    klog("\n");
    kprintf("\n");
    relogio_dormir_ms(100);
    confere(processo_por_pid(a->pid) == NULL && processo_por_pid(b->pid) == NULL,
            "tarefas terminadas foram recolhidas");
}

/* H12: programas no anel 3. */
extern const uint8_t anel3_ola[], anel3_ola_fim[], anel3_cli[], anel3_cli_fim[],
                     anel3_espiao[], anel3_espiao_fim[];

static processo_t *rodar_no_anel3(const char *nome, const uint8_t *ini, const uint8_t *fim)
{
    processo_t *p = processo_novo(nome, -1);   /* pai -1: este autoteste recolhe */
    if (!p) return NULL;
    p->espaco = espaco_criar();
    espaco_alocar(p->espaco, USUARIO_INICIO, PAGINA, PG_USUARIO | PG_ESCRITA);
    espaco_alocar(p->espaco, USUARIO_FIM - PAGINA, PAGINA, PG_USUARIO | PG_ESCRITA);
    /* O kernel enxerga a página física pelo mapeamento 1:1 (D008). */
    memcpy((void *)(uintptr_t)espaco_traduzir(p->espaco, USUARIO_INICIO, NULL), ini, (uint32_t)(fim - ini));
    arquivos_herdar(p, NULL);   /* terminal como entrada 0 e saídas 1 e 2 */
    processo_preparar_usuario(p, USUARIO_INICIO, USUARIO_FIM);
    return p;
}

static int esperar_zumbi(processo_t *p)
{
    while (p->estado != P_ZUMBI)
        cpu_espera();
    int c = p->codigo_saida;
    klog("[TESTE] %s terminou: codigo %d, motivo '%s'\n", p->nome, c, p->motivo);
    processo_liberar(p);
    return c;
}

static void teste_anel3(void)
{
    estat_memoria_t m0, m1;
    memoria_estatisticas(&m0);
    processo_t *a = rodar_no_anel3("ola-anel3", anel3_ola, anel3_ola_fim);
    confere(a != NULL, "processo criado");
    if (!a) return;
    int c = esperar_zumbi(a);
    confere(c == 42, "escrever funcionou e recusou o ponteiro para o kernel (saída 42)");

    processo_t *b = rodar_no_anel3("cli-anel3", anel3_cli, anel3_cli_fim);
    c = esperar_zumbi(b);
    confere(c == SAIDA_ENCERRADO_PELO_KERNEL, "cli no anel 3 encerrou só aquele processo");

    processo_t *d = rodar_no_anel3("espiao-anel3", anel3_espiao, anel3_espiao_fim);
    c = esperar_zumbi(d);
    confere(c == SAIDA_ENCERRADO_PELO_KERNEL, "ler a memória do kernel encerrou o processo");

    /* Páginas que o kmalloc pegou para blocos pequenos ficam com ele (D008). */
    memoria_estatisticas(&m1);
    confere(m1.paginas_livres + (m1.heap_paginas - m0.heap_paginas) == m0.paginas_livres,
            "a memória dos três processos foi toda devolvida");
    kprintf("O kernel continua de pé depois dos dois programas com erro.\n");
}

/* H17: fundo colorido e formas na resolução detectada. */
static void teste_grafico(void)
{
    confere(video_ativo() || video_iniciar(), "modo gráfico ligado pelo estágio 2");
    if (!video_ativo()) return;
    int l = (int)video_largura(), a = (int)video_altura();
    klog("[TESTE] video %dx%d\n", l, a);
    video_degrade_vertical(0, 0, l, a, RGB(0, 120, 60), RGB(10, 40, 120));   /* verde para azul */
    video_retangulo(40, 40, 200, 120, RGB(255, 210, 0));                     /* amarelo cheio */
    video_caixa(280, 40, 200, 120, RGB(255, 255, 255), RGB(200, 0, 0));      /* branco com borda vermelha */
    video_moldura(520, 40, 200, 120, 6, RGB(0, 0, 0));                        /* só a borda, grossa */
    for (int i = 0; i <= 10; i++)                                              /* leque de linhas */
        video_linha(40, a - 40, 40 + i * (l / 2 - 40) / 10, a / 2, RGB(255, 255, 255));
    video_linha(l / 2, a / 2, l - 40, a - 40, RGB(255, 80, 80));
    video_linha(l - 40, a / 2, l / 2, a - 40, RGB(80, 255, 80));
    for (int i = 0; i < 8; i++)                                                /* barras de cor */
        video_retangulo(l / 2 + 20 + i * 30, 200, 26, 80, RGB((i & 1) * 255, ((i >> 1) & 1) * 255, ((i >> 2) & 1) * 255));
    video_mostrar_tudo();
}

/* H19-fix: simula no QEMU o que aconteceu no G470 - a interrupção do mouse
 * não chega. Os bytes do mouse ficam na fila do controlador; o sistema tem
 * que seguir com o teclado funcionando (e o mouse andando pelo relógio). */
static void teste_sem_irq12(void)
{
    pic_desabilitar(12);
    kprintf("Autoteste: IRQ 12 (mouse) bloqueada de propósito.\n");
}

/* H31: lê e grava setores do pendrive pelo Bulk-Only/SCSI. A VPS confere
 * o arquivo da imagem depois, byte a byte. */
static void teste_usb_setores(void)
{
    /* Só pendrive: este teste grava setores, e um disco interno (hd0, hd1)
     * é o HD de quem testa. */
    disco_t *d = NULL;
    for (int i = 0; !d && disco_indice(i); i++)
        if (!disco_indice(i)->interno) d = disco_indice(i);
    confere(d != NULL, "um pendrive foi reconhecido");
    if (!d) return;
    uint8_t *buf = kmalloc(64 * 512), *volta = kmalloc(64 * 512);
    /* Leitura: os 64 primeiros setores; a soma vai para a serial. */
    confere(d->ler(d, 0, 64, buf) == 0, "leitura de 64 setores");
    uint32_t soma = 0;
    for (int i = 0; i < 64 * 512; i++) soma = soma * 31 + buf[i];
    klog("[TESTE] soma setores 0-63: %x\n", soma);
    /* Gravação: um padrão em 40 setores a partir do 100000 (área livre do FAT32). */
    for (int i = 0; i < 40 * 512; i++) buf[i] = (uint8_t)(i * 7 + i / 512);
    confere(d->escrever(d, 100000, 40, buf) == 0, "gravação de 40 setores");
    memset(volta, 0, 40 * 512);
    confere(d->ler(d, 100000, 40, volta) == 0, "releitura");
    confere(memcmp(buf, volta, 40 * 512) == 0, "o que foi lido é igual ao que foi gravado");
    /* Um setor só, no fim do disco. */
    memset(buf, 0xA5, 512);
    confere(d->escrever(d, d->setores - 1, 1, buf) == 0, "gravação do último setor");
    kfree(buf); kfree(volta);
}

typedef struct {
    const char *nome;
    void (*funcao)(void);
} autoteste_t;

static const autoteste_t testes[] = {
    { "divisao", teste_divisao },
    { "relogio", teste_relogio },
    { "memoria", teste_memoria },
    { "teclado", teste_teclado },
    { "sgfs", teste_sgfs },
    { "paginacao", teste_paginacao },
    { "falta_pagina", teste_falta_pagina },
    { "tarefas", teste_tarefas },
    { "anel3", teste_anel3 },
    { "grafico", teste_grafico },
    { "sem_irq12", teste_sem_irq12 },
    { "usb_setores", teste_usb_setores },
};

void autoteste_executar(const char *nome)
{
    for (unsigned i = 0; i < sizeof testes / sizeof testes[0]; i++) {
        if (txt_compara(nome, testes[i].nome) == 0) {
            klog("[TESTE] inicio %s\n", nome);
            falhas = 0;
            testes[i].funcao();
            if (falhas)
                klog("[TESTE] %s FALHOU (%d)\n", nome, falhas);
            else
                klog("[TESTE] %s ok\n", nome);
            return;
        }
    }
    klog("[TESTE] desconhecido: %s\n", nome);
}
