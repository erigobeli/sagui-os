#!/bin/sh
# Compara o descompressor Brotli com o módulo brotli do Python em vários textos e níveis.
cd "$(dirname "$0")"; mkdir -p /tmp/br
gcc -O1 -g -w -DMOTOR_HOST -I../motor/host -I../../userland/inc -o /tmp/br/tb teste_brotli.c ../../userland/libsagui/brotli.c || exit 1
python3 - <<'PY'
import brotli, subprocess, os, random
random.seed(1)
amostras = {
  'html': open('../../userland/programas/cipo.c','rb').read(),
  'bin': bytes(random.getrandbits(8) for _ in range(70000)),
  'pal': b'the time of the people and the world ' * 900 + b'The Time Of', 
  'vazio': b'', 'um': b'a',
  'fonte': open('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf','rb').read(),
}
ruins = 0
for nome, d in amostras.items():
    for q in (0, 1, 5, 9, 11):
        for modo in (brotli.MODE_GENERIC, brotli.MODE_TEXT, brotli.MODE_FONT):
            c = brotli.compress(d, quality=q, mode=modo)
            open('/tmp/br/x.br','wb').write(c)
            r = subprocess.run(['/tmp/br/tb','/tmp/br/x.br','/tmp/br/x.out'],capture_output=True,text=True)
            ok = r.stdout.startswith('ok') and open('/tmp/br/x.out','rb').read() == d
            if not ok: ruins += 1; print('ERRO', nome, q, modo, r.stdout.strip(), len(c))
print('ruins', ruins)
PY
