/* js.h - interpretador JavaScript mínimo para o Sagui Navegador.
 *
 * Suporta um subconjunto muito pequeno de JavaScript "antigo":
 *   - alert("texto")        - mostra uma caixa de mensagem
 *   - document.write("texto") - escreve texto no documento (apenas no carregamento)
 *   - document.getElementById("id") - retorna um elemento (simplificado)
 *   - var x = valor         - variáveis globais simples (números e strings)
 *   - console.log("texto")  - registra no log do sistema
 *   - function() { ... }    - funções sem parâmetros (apenas declaração)
 *
 * Isso é intencionalmente limitado: o SaguiOS não é um navegador completo.
 * O objetivo é fazer páginas simples funcionarem, não competir com o V8.
 *
 * Sem libc, apenas syscalls int 0x80 e libsagui. */

#ifndef SAGUI_JS_H
#define SAGUI_JS_H

#include "sagui.h"
#include "navegador.h"

/* Executa um bloco de JavaScript extraído de uma tag <script>.
 * Devolve 0 se executou com sucesso, -1 se não entendeu nada. */
int js_executar_bloco(const char *codigo, int tamanho);

/* Executa um único comando JavaScript (linha por linha). */
int js_executar_comando(const char *comando);

/* Mostra uma caixa de alerta JS na janela do navegador. */
void js_alerta(const char *texto);

#endif
