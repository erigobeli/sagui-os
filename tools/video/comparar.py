"""Compara dois YUV 4:2:0 crus quadro a quadro (PSNR da luma e pixels diferentes)."""
import math, sys
a, b, w, h = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
q = w * h * 3 // 2
da, db = open(a, 'rb').read(), open(b, 'rb').read()
n = min(len(da), len(db)) // q
print(f"quadros: {len(da)//q} vs {len(db)//q}")
ruins = 0
for i in range(n):
    x, y = da[i*q:(i+1)*q], db[i*q:(i+1)*q]
    dif = sum(1 for k in range(q) if x[k] != y[k])
    if dif:
        mse = sum((x[k]-y[k])**2 for k in range(w*h)) / (w*h)
        psnr = 99 if mse == 0 else 10*math.log10(255*255/mse)
        k0 = next(k for k in range(q) if x[k] != y[k])
        onde = f"y=({k0%w},{k0//w}) mb=({k0%w//16},{k0//w//16})" if k0 < w*h else f"croma byte {k0-w*h}"
        print(f"quadro {i}: {dif} pixels diferentes, PSNR Y {psnr:.1f}, primeiro {onde}")
        ruins += 1
        if ruins > 8: break
print("IGUAIS" if ruins == 0 else f"{ruins}+ quadros diferentes")
