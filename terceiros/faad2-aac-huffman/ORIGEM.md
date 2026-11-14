# FAAD2: livros de Huffman do AAC

**Code from FAAD2 is copyright (c) Nero AG, www.nero.com**

- **De onde veio:** as tabelas de decodificação de Huffman do
  [FAAD2](https://github.com/knik0/faad2) (M. Bakker, Ahead Software AG /
  Nero AG). Os códigos são os livros de Huffman da norma do AAC (ISO/IEC
  14496-3, anexo 4.A), mas os números foram tirados das tabelas do FAAD2.
- **Licença:** GPL 2 **ou (à sua escolha) qualquer versão posterior** — por
  isso compatível com a GPL 3 do SaguiOS. Conferido no `COPYING` do FAAD2
  (copiado aqui, com o preâmbulo) e no arquivo de copyright do Debian, que
  registra a licença como `GPL-2+`. O preâmbulo do FAAD2 vale e fica
  registrado:
  - "Any non-GPL usage of this software or parts of this software is strictly
    forbidden." (o SaguiOS é GPL 3: o uso é GPL);
  - a "mensagem de copyright apropriada" da seção 2c da GPL 2 deve ser:
    "Code from FAAD2 is copyright (c) Nero AG, www.nero.com" — ela está no
    topo deste arquivo, em `aac_huff.h`, em `userland/video/aac.c` e no
    `CREDITOS.md`.
- **Como chegou aqui:** veio no pull request #6 (o tocador de vídeo, de
  Robson de Lima Barbosa) como `libsagui/aac_huff.h`, já com o crédito ao
  FAAD2. Na 2.1 (H82, D043) mora aqui; `userland/video/aac.c` o inclui.
- **O que mudou:** nada. O formato `(tamanho << 24) | código` por livro, na
  ordem da norma, é do #6 (conferido como livre de prefixo e completo pela
  ferramenta `tools/video/ext_aac.py` do #6, que não veio para a 2.1).
