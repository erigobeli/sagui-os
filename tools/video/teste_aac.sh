#!/bin/sh
# Codifica áudio de teste com o AAC do ffmpeg em várias opções e compara com aac.c.
cd "$(dirname "$0")"
gcc -O2 -DH264_HOST -I../../userland/inc -w -o /tmp/v/ta teste_aac.c ../../userland/libsagui/aac.c ../../userland/libsagui/mp4.c -lm || exit 1
for o in "-aac_pns 0 -aac_is 0 -aac_tns 0 -aac_ms 0" "-aac_pns 0 -aac_is 0 -aac_tns 0" "-aac_pns 0 -aac_is 0" "-aac_pns 0" ""; do
  for r in 44100 22050 48000 32000; do
    for ch in 2 1; do
      ffmpeg -loglevel error -y -f lavfi -i "sine=f=440:d=4" -f lavfi -i "anoisesrc=d=4:a=0.15:c=pink" \
        -f lavfi -i "aevalsrc=exprs='0.4*sin(2*PI*t*t*900)*lt(mod(t\,1)\,0.08)':d=4" \
        -filter_complex "[0][1][2]amix=inputs=3,aecho=0.8:0.5:60:0.4" -ar $r -ac $ch -c:a aac -b:a 96k $o /tmp/v/s.mp4 || exit 1
      ffmpeg -loglevel error -y -i /tmp/v/s.mp4 -f s16le -ac 2 /tmp/v/s_ref.raw
      echo "== $r Hz $ch ch $o: $(/tmp/v/ta /tmp/v/s.mp4 /tmp/v/s.raw 2>&1 | tail -1)"
      python3 comparar_pcm.py /tmp/v/s.raw /tmp/v/s_ref.raw | tail -1
    done
  done
done
