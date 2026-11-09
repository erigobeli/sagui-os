/* o que o motor JS chama da libsagui e não importa no host */
#include "sagui.h"
void http_preparar(const char *m, const char *c, const void *b, int n) { (void)m; (void)c; (void)b; (void)n; }
void cookie_guardar(const char *u, const char *s, bool j) { (void)u; (void)s; (void)j; }
int cookie_texto(const char *u, char *o, int max, bool j) { (void)u; (void)j; if (max) o[0] = 0; return 0; }
void *fonte_sagui(void) { return 0; }
