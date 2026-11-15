/* js.h - interpretador JavaScript mínimo para o Sagui Navegador.
 *
 * Suporta um subconjunto de JavaScript "antigo":
 *   - alert("texto")        - mostra caixa de mensagem
 *   - confirm("texto")      - mostra caixa sim/não (sempre devolve true)
 *   - prompt("texto")       - mostra caixa de entrada (sempre devolve "")
 *   - console.log/warn/error("texto") - registra no log do sistema
 *   - document.write("texto") - escreve texto no documento (no carregamento)
 *   - document.getElementById("id") - busca elemento por id
 *   - document.getElementsByTagName("tag") - busca elementos por tag
 *   - document.getElementsByClassName("cls") - busca elementos por classe
 *   - var/let/const x = valor         - variáveis globais (números e strings)
 *   - function nome() { ... } - declara função
 *   - nome()                - chama função declarada
 *   - setTimeout(codigo, ms) - executa código após delay (apenas registra)
 *   - setInterval(codigo, ms) - executa repetidamente (apenas registra)
 *   - Math.random()         - número aleatório 0-1
 *   - Math.floor(x)         - arredonda para baixo
 *   - Math.ceil(x)          - arredonda para cima
 *   - Math.round(x)         - arredonda
 *   - Math.max(a, b)        - máximo
 *   - Math.min(a, b)        - mínimo
 *   - parseInt(s)           - converte string para inteiro
 *   - parseFloat(s)         - converte string para float
 *   - isNaN(x)              - verifica se não é número
 *   - isFinite(x)           - verifica se é finito
 *   - element.style.propriedade - modifica estilo (simplificado)
 *   - element.innerHTML     - modifica conteúdo (simplificado)
 *   - element.className     - modifica classe CSS
 *   - element.id            - modifica id
 *   - addEventListener(evento, func) - registra evento (apenas registra)
 *
 * É intencionalmente limitado: o SaguiOS não é um navegador completo.
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

/* Define uma variável global JS. */
void js_definir_var_js(const char *nome, const char *valor);

/* Busca um elemento por id (simplificado: devolve NULL). */
no_t *js_document_get_element_by_id(documento_t *doc, const char *id);

/* Busca elementos por nome de tag (simplificado: devolve NULL). */
no_t *js_document_get_elements_by_tag_name(documento_t *doc, const char *tag);

/* Busca elementos por classe CSS (simplificado: devolve NULL). */
no_t *js_document_get_elements_by_class_name(documento_t *doc, const char *classe);

/* Adiciona um ouvinte de evento a um elemento (apenas registra). */
int js_element_add_event_listener(no_t *elemento, const char *evento, const char *funcao);

#endif
