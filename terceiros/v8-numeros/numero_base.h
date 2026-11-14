/* numero_base.h - número em outra base (2 a 36), como o V8 faz (BSD;
 * veja numero_base.c, LICENSE e ORIGEM.md nesta pasta). */
#ifndef TERCEIROS_V8_NUMERO_BASE_H
#define TERCEIROS_V8_NUMERO_BASE_H
/* v finito, base de 2 a 36; "saida" precisa de 2200 bytes. Devolve o tamanho
 * do texto, que fica no começo de "saida" (sem zero no fim). piso e resto são
 * Math.floor e o % do JavaScript (o SaguiOS não tem libm). */
int v8_numero_em_base(double v, int base, char *saida, double (*piso)(double), double (*resto)(double, double));
#endif
