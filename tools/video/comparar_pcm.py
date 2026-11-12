"""Compara dois s16le estéreo: erro máximo, SNR e atraso."""
import sys, array, math
a = array.array('h', open(sys.argv[1], 'rb').read()); b = array.array('h', open(sys.argv[2], 'rb').read())
print(f"amostras: {len(a)//2} vs {len(b)//2}")
n = min(len(a), len(b))
best = None
for atraso in (0, 2048, -2048, 4096, -4096):
    s = e = mx = 0
    for i in range(0, n - abs(atraso)):
        x = a[i + atraso] if atraso > 0 else a[i]; y = b[i] if atraso > 0 else b[i - atraso]
        d = x - y; s += y*y; e += d*d; mx = max(mx, abs(d))
    snr = 99 if e == 0 else 10*math.log10(max(s, 1)/e)
    if best is None or snr > best[1]: best = (atraso, snr, mx)
print(f"atraso {best[0]//2} amostras, SNR {best[1]:.1f} dB, erro max {best[2]}")
