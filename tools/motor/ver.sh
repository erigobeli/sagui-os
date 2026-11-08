#!/bin/sh
# Compila o motor no Linux e gera PNG de cada página: sh ver.sh pagina.html [largura]
cd "$(dirname "$0")"
gcc -O1 -g -w -DMOTOR_HOST -Ihost -I../../userland/inc -o /tmp/v/tm teste_motor.c ../../userland/libsagui/motor.c ../../userland/libsagui/fonte.c ../../userland/libsagui/brotli.c ../../userland/libsagui/imagem.c ../../userland/libsagui/quebra.c ../../userland/libsagui/canvas2d.c -msse2 ../../build/gerado/fonte8x16.c || exit 1
/tmp/v/tm "$1" /tmp/v/m.ppm ${2:-846} $3 && python3 -c "from PIL import Image; Image.open('/tmp/v/m.ppm').save('/tmp/v/m.png')"
