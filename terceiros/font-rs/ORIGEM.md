# font-rs: rasterizador por área acumulada

- **De onde veio:** o rasterizador do [font-rs](https://github.com/raphlinus/font-rs),
  de Raph Levien (Copyright 2015 Google Inc.), em Rust.
- **Licença:** Apache License 2.0 (o texto está em `LICENSE`, copiado do
  repositório). Compatível com a GPL 3 do SaguiOS. Não há arquivo NOTICE.
- **Como chegou aqui:** no pull request #6 (o navegador Cipó, de Robson de Lima
  Barbosa) a IA que ajudou o Robson reproduziu de memória, em C, a função que
  acumula a área coberta por cada segmento (`linha()` em `fonte.c` e
  `r_linha()` em `canvas2d.c`) e a soma corrida que dá a cobertura. A revisão
  do #6 reconheceu o algoritmo e as contas (as mesmas variáveis e constantes).
  Na 2.1 (H82, D043) esse trecho saiu dos arquivos do Cipó e mora aqui.
- **Versão:** não é cópia de uma versão: é a forma do `accumulate`/`draw_line`
  do font-rs como estava no repositório (2016-2017), reescrita em C.
- **O que mudou em relação ao original:**
  - em C, com `float` (fontes) e `double` (Canvas), sem SIMD;
  - `fontrs_linha_recortada`: variação para o Canvas que recorta à caixa do
    desenho (o que passa à esquerda conta na coluna 0; à direita some);
  - `fontrs_acumular_regra`: a soma corrida com a regra "par ou ímpar"
    (`fill-rule: evenodd`) do Canvas e do SVG, acrescentada no #6.
- **Quem usa:** `userland/navegador/fontes/fonte.c` (glifos TrueType e CFF) e
  `userland/navegador/graficos/canvas2d.c` (caminhos do Canvas e do SVG).

Atenção (Apache 2.0, seção 4): os arquivos desta pasta foram modificados em
relação ao original, como descrito acima.
