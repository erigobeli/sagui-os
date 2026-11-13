/* sizeof: o tamanho em bytes de tipos e de variáveis. */
#include "sagui.h"

struct simples { char a; };
struct misturado { char a; int b; char c; short d; };
struct com_longo { char a; long long b; };
union junto { char a[5]; int b; };

static int conta_elementos(int v[]) { return (int)sizeof v; }     /* aqui v é só um ponteiro */

int principal(int argc, char **argv)
{
    int v[10];
    char t[] = "sagui";
    struct misturado m;
    int *p = v;
    (void)argc; (void)argv; (void)m; (void)p;
    imprimir("%d %d %d %d %d %d\n", (int)sizeof(char), (int)sizeof(short), (int)sizeof(int), (int)sizeof(long),
             (int)sizeof(long long), (int)sizeof(void *));
    imprimir("%d %d %d %d\n", (int)sizeof(struct simples), (int)sizeof(struct misturado), (int)sizeof(struct com_longo),
             (int)sizeof(union junto));
    imprimir("%d %d %d %d\n", (int)sizeof v, (int)sizeof v[0], (int)(sizeof v / sizeof *v), conta_elementos(v));
    imprimir("%d %d %d\n", (int)sizeof t, (int)sizeof "abc", (int)sizeof 'a');
    imprimir("%d %d %d\n", (int)sizeof m.b, (int)sizeof(m.d), (int)sizeof p);
    imprimir("%d %d\n", (int)sizeof(int[3][4]), (int)sizeof(char *[5]));
    int i = 0;
    unsigned s = sizeof(i++);         /* o que está dentro do sizeof não roda */
    imprimir("%u %d\n", s, i);
    imprimir("%d %d\n", (int)sizeof(uint8_t) + (int)sizeof(uint16_t) + (int)sizeof(uint32_t) + (int)sizeof(uint64_t), (int)sizeof(bool));
    return 0;
}
