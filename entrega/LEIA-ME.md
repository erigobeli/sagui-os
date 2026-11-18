# Imagens do SaguiOS

As imagens prontas (`saguios.img` para pendrive e `saguios.vdi` para o
VirtualBox) ficam nas **Releases** do GitHub, com o SHA-256 de cada arquivo:

https://github.com/erigobeli/sagui-os/releases

As versões de teste saem como "pre-release" na mesma página.

Esta pasta é onde o `make entrega FASE=N` copia as imagens no computador de
quem compila; elas não vão mais para o git, porque cada imagem tem uns 7 MB e
todo mundo que clonasse o projeto baixaria todas. As imagens antigas (das
fases da 1.0 até a 2.1) continuam no histórico do git.
