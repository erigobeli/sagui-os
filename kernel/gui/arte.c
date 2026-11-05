/* arte.c - os desenhos da área de trabalho, em pixel art feita neste projeto.
 *
 * Cada desenho é uma lista de linhas de texto; cada letra é uma cor da
 * paleta abaixo e '.' é transparente. Desenhar = percorrer as letras e pintar
 * quadradinhos do tamanho da escala. */
#include "arte.h"

/* O sagui-de-tufos-brancos: cabeça escura com a mancha branca na testa, os
 * tufos brancos nas orelhas, corpo cinza e a cauda listrada. 32x32. */
static const char *const sagui[] = {
    "................................",
    "...........EEEEEEEEEE...........",
    ".........EEEEEEEEEEEEEE.........",
    "..BBB...EEEEEBBBBBEEEEEE..BBB...",
    ".BBBBB.EEEEEEEBBBEEEEEEEEBBBBB..",
    "BBBBBBBEEEEEEEEBEEEEEEEEEBBBBBB.",
    "BBBBBBEEERRRRRRRRRRRRRREEEBBBBB.",
    ".BBBBBEERRPPPRRRRRRPPPRREEBBBBB.",
    "..BBBBEERRPbPRRRRRRPbPRREEBBBB..",
    "...BBBEERRPPPRRRRRRPPPRREEBBB...",
    ".....EEERRRRRRRRRRRRRRRREEE.....",
    "......EERRRRRRPPPRRRRRRREE......",
    "......EEERRRRRRRRRRRRRREEE......",
    ".......EEERRRRPPPPRRRREEE.......",
    "........EEEERRRRRRRREEEE........",
    ".........EEEEEEEEEEEEEE.........",
    "........MMMMMMMMMMMMMMMM........",
    ".......MMMCCCCCCCCCCCCMMM.......",
    "......MMMCCCCCCCCCCCCCCMMM......",
    ".....MMMCCCCCccccccCCCCCMMM.....",
    ".....MMCCCCCccccccccCCCCCMM.....",
    "....MMMCCCCccccccccccCCCCMMM....",
    "....MMCCCCCccccccccccCCCCCMM....",
    "....MMCCCCCCccccccccCCCCCCMM...E",
    "....MMMCCCCCCCCCCCCCCCCCCMMM..EC",
    ".....MMMCCCCCCCCCCCCCCCCMMM..EC.",
    "......MMMMCCCCCCCCCCCCMMMM..CE..",
    ".....EEMMMMMMMMMMMMMMMMMMEE.EC..",
    "....EEEE.MMMM......MMMM.EEECE...",
    "...EEEE..EEE........EEE..CECE...",
    "...........................EC...",
    "................................",
};

/* Os ícones da área de trabalho vêm de tools/mkarte.py. Esta cabecinha fica
 * aqui porque a barra superior a usa em tamanho pequeno. */
/* Sagui 3D (B4): corredor de pedra em perspectiva, céu em cima, grama
 * embaixo e uma banana no fundo. */
static const char *const icone_sobre[] = {   /* o mascote pequeno: só a cabeça */
    "................................",
    "................................",
    "...........EEEEEEEEEE...........",
    ".........EEEEEEEEEEEEEE.........",
    "..BBB...EEEEEBBBBBEEEEEE..BBB...",
    ".BBBBB.EEEEEEEBBBEEEEEEEEBBBBB..",
    "BBBBBBBEEEEEEEEBEEEEEEEEEBBBBBB.",
    "BBBBBBEEERRRRRRRRRRRRRREEEBBBBB.",
    ".BBBBBEERRPPPRRRRRRPPPRREEBBBBB.",
    "..BBBBEERRPbPRRRRRRPbPRREEBBBB..",
    "...BBBEERRPPPRRRRRRPPPRREEBBB...",
    ".....EEERRRRRRRRRRRRRRRREEE.....",
    "......EERRRRRRPPPRRRRRRREE......",
    "......EEERRRRRRRRRRRRRREEE......",
    ".......EEERRRRPPPPRRRREEE.......",
    "........EEEERRRRRRRREEEE........",
    ".........EEEEEEEEEEEEEE.........",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
    "................................",
};

const desenho_t arte_sagui            = { sagui, 32, 32 };
const desenho_t arte_icone_sobre      = { icone_sobre, 32, 32 };

/* Paleta: letra -> cor. Devolve false para transparente. */
bool arte_cor(char letra, uint32_t *cor)
{
    switch (letra) {
    case 'P': *cor = 0x141210; return true;   /* preto (contorno) */
    case 'E': *cor = 0x3C2D23; return true;   /* marrom-escuro (cabeça, cauda) */
    case 'M': *cor = 0x6E553C; return true;   /* marrom */
    case 'C': *cor = 0x968C7D; return true;   /* cinza */
    case 'c': *cor = 0xBEB4A5; return true;   /* cinza-claro (peito) */
    case 'B': *cor = 0xF5F5F0; return true;   /* branco (tufos, testa) */
    case 'R': *cor = 0xAA7864; return true;   /* rosado (rosto) */
    case 'b': *cor = 0xFFFFFF; return true;   /* brilho no olho */
    case 'Y': *cor = 0xFFD52E; return true;   /* amarelo */
    case 'A': *cor = 0xD9A400; return true;   /* amarelo-escuro */
    case 'G': *cor = 0x3CDC6E; return true;   /* verde */
    case 'V': *cor = 0x2A6FD8; return true;   /* azul */
    case 'k': *cor = 0x5A564E; return true;   /* pedra escura (Sagui 3D) */
    case 'L': *cor = 0x8F8A7C; return true;   /* pedra clara */
    case 's': *cor = 0x7EC8E3; return true;   /* céu */
    case 'g': *cor = 0x0B7A3E; return true;   /* verde do SaguiOS */
    default: return false;
    }
}
