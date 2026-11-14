# V8: número em outra base (Number.prototype.toString)

- **De onde veio:** a função `DoubleToRadixCString` do motor de JavaScript
  [V8](https://v8.dev) (Copyright 2006-2011, the V8 project authors).
- **Licença:** BSD de 3 cláusulas (`LICENSE`, o LICENSE.v8 do V8). Compatível
  com a GPL 3.
- **Como chegou aqui:** no pull request #6 (o navegador Cipó, de Robson de Lima
  Barbosa) o `n_numstr` de `js_nativo.h` (linhas 757 a 800 do #6) seguia o
  algoritmo do V8 — o próprio comentário dizia "mesmo algoritmo do V8". Na
  2.1 (H82, D043) o algoritmo saiu do motor de JavaScript e mora aqui;
  `js_nativo.h` só chama `v8_numero_em_base()`.
- **O que mudou em relação ao original:** em C, sem a biblioteca do V8; o
  `floor` e o `%` do JavaScript chegam como funções (o SaguiOS não tem libm);
  o texto é devolvido no começo do buffer de quem chama.
- **Teste:** `tests/h82_cipo.py` (`(255).toString(16)`, `(0.5).toString(2)`,
  `(-10.25).toString(8)`, `(0.1).toString(3)`).
