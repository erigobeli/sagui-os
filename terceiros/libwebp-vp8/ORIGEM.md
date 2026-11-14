# libwebp: WebP com perdas (quadro-chave VP8)

- **De onde veio:** o decodificador VP8 do [libwebp](https://chromium.googlesource.com/webm/libwebp)
  (Copyright (c) 2010, Google Inc.), que segue a RFC 6386.
- **Licença:** BSD de 3 cláusulas (`LICENSE`, o COPYING do libwebp) e a
  concessão de patentes do projeto WebM (`PATENTS`). Compatível com a GPL 3.
- **Como chegou aqui:** no pull request #6 (o navegador Cipó, de Robson de Lima
  Barbosa) a IA que ajudou o Robson escreveu o decodificador VP8 de
  `imagem.c` (linhas 651 a 1001 do #6) seguindo a estrutura do libwebp, e
  extraiu as tabelas de probabilidades de `vp8_tabelas.h` do libwebp 1.4.0
  (como o próprio arquivo diz). Na 2.1 (H82, D043) os dois saíram do Cipó e
  moram aqui.
- **O que mudou em relação ao original:** é C escrito de novo, bem menor que
  o libwebp: só o quadro-chave com perdas (o chunk `VP8 `), sem o filtro de
  suavização entre blocos, sem WebP sem perdas (`VP8L`), sem alfa (`ALPH`) e
  sem animação. A saída é `0xAARRGGBB` opaca. A função de entrada é
  `webp_ler()` (`vp8.h`), chamada por `userland/navegador/imagens.c`.
- **Teste:** `tests/h82_cipo.py` decodifica um WebP feito pelo Pillow e
  compara com o do Pillow (diferença média pequena, por causa do filtro que
  falta).
