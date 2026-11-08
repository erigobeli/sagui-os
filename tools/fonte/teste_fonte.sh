#!/bin/sh
# Fonte: TTF, WOFF e WOFF2 da mesma face têm que desenhar igual; largura e tinta perto do FreeType (PIL).
cd "$(dirname "$0")"; mkdir -p /tmp/fo
gcc -O1 -g -w -DMOTOR_HOST -msse2 -I../motor/host -I../../userland/inc -o /tmp/fo/tf teste_fonte.c ../../userland/libsagui/fonte.c ../../userland/libsagui/brotli.c ../../userland/libsagui/imagem.c 2>&1 | head -20 || exit 1
python3 - <<'PY'
import subprocess, os
from fontTools.ttLib import TTFont
from PIL import Image, ImageFont, ImageDraw
TXT = "Sagui OS: o navegador Cipo desenha fontes da web! 0123 (Wg) AVa"
for nome in ['/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', '/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf', '/usr/share/fonts/opentype/urw-base35/URWBookman-Light.otf', '/usr/share/fonts/opentype/tlwg/Loma-Bold.otf']:
    base = os.path.basename(nome)[:-4]
    ext0 = nome[-3:]
    for fl in ('woff', 'woff2'):
        ft = TTFont(nome); ft.flavor = fl; ft.save(f'/tmp/fo/{base}.{fl}')
    res = {}
    for ext, arq in ((ext0, nome), ('woff', f'/tmp/fo/{base}.woff'), ('woff2', f'/tmp/fo/{base}.woff2')):
        r = subprocess.run(['/tmp/fo/tf', arq, '24', TXT, f'/tmp/fo/{base}-{ext}.pgm'], capture_output=True, text=True)
        print(base, ext, r.stdout.strip(), r.stderr.strip()[:200])
        res[ext] = open(f'/tmp/fo/{base}-{ext}.pgm','rb').read() if r.returncode == 0 else None
    print('  iguais:', res[ext0] == res['woff'] == res['woff2'])
    pf = ImageFont.truetype(nome, 24)
    print('  freetype largura', round(pf.getlength(TXT)))
    im = Image.new('L', (1200, 48)); ImageDraw.Draw(im).text((2, 2), TXT, font=pf, fill=255)
    print('  freetype tinta', sum(im.getdata()))
    Image.open(f'/tmp/fo/{base}-woff2.pgm').save(f'/tmp/fo/{base}.png')
PY
