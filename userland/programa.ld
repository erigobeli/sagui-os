/* programa.ld - como os programas do SaguiOS são ligados.
 * Endereço 0x40000000 (PRD 4); dados numa página separada do código. */
ENTRY(_inicio)
SECTIONS
{
    . = 0x40000000;
    .text : { *(.text.inicio) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    . = ALIGN(4096);
    _dados_ini = .;     /* o coletor de lixo do JavaScript do Cipó varre os dados (H82) */
    /* O estado de cada aba do Cipó (navegador.c): as variáveis da página
     * (as marcadas no navegador.c e as do JavaScript, do motor e dos
     * gráficos) ficam em duas faixas que o navegador guarda e troca. Nos
     * outros programas as faixas ficam vazias. */
    .data : {
        _aba_dados_ini = .;
        *(.data.aba)
        */navegador/js/js.o(.data .data.*) */navegador/motor/motor.o(.data .data.*)
        */navegador/graficos/*.o(.data .data.*)
        _aba_dados_fim = .;
        *(.data .data.*)
    }
    .bss : {
        _aba_bss_ini = .;
        *(.bss.aba)
        */navegador/js/js.o(.bss .bss.*) */navegador/motor/motor.o(.bss .bss.*)
        */navegador/graficos/*.o(.bss .bss.*)
        _aba_bss_fim = .;
        *(.bss .bss.*) *(COMMON)
    }
    _dados_fim = .;
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
