import re, sys
D = sys.argv[1]
def tab(f, nome):
    s = open(f"{D}/{f}").read()
    s = re.sub(r'/\*.*?\*/', '', s, flags=re.S)
    m = re.search(r'\b' + nome + r'\s*\[\][^=]*=\s*\{(.*?)\n\};', s, re.S)
    rows = re.findall(r'\{([^{}]*(?:\{[^{}]*\}[^{}]*)?)\}', m.group(1))
    return [[int(x) for x in re.findall(r'-?\d+', r)] for r in rows]
dim = [0,4,4,4,4,2,2,2,2,2,2,2]; sgn = [0,1,1,0,0,1,1,0,0,0,0,0]; lav = [0,1,1,2,2,4,4,7,7,12,12,16]
N = [0,5,5,0,5,0,5,0,5,0,6,5]
out = ["/* Huffman codebooks of ISO/IEC 14496-3 (AAC): (length << 24) | codeword,\n * per codebook index in spec order. Numeric data taken from FAAD2's\n * decoding tables (Code from FAAD2 is copyright (c) Nero AG, www.nero.com);\n * checked prefix-free and complete by tools/video/ext_aac.py. */"]
for cb in range(12):
    E = {}
    if cb == 0:
        t = tab("hcb_sf.h", "hcb_sf")
        def sf(o, l, c):
            if not t[o][1]: E[t[o][0]] = (l, c); return
            sf(o + t[o][0], l+1, c<<1); sf(o + t[o][1], l+1, (c<<1)|1)
        sf(0, 0, 0); qtd = 121
    else:
        m = 2*lav[cb]+1 if sgn[cb] else lav[cb]+1; off = lav[cb] if sgn[cb] else 0
        qtd = m**dim[cb]
        def ix(v):
            r = 0
            for x in v[:dim[cb]]: r = r*m + x + off
            return r
        if N[cb]:
            t1 = tab(f"hcb_{cb}.h", f"hcb{cb}_1"); t2 = tab(f"hcb_{cb}.h", f"hcb{cb}_2")
            for cw, (o, ex) in enumerate(t1):
                for e in range(1 << ex):
                    r = t2[o+e]; bits = r[0]; tot = N[cb]+ex
                    if bits == 0 or bits > tot: continue
                    k = ix(r[1:]); c = ((cw << ex) | e) >> (tot - bits)
                    if k in E and E[k] != (bits, c): sys.exit(f"cb{cb} conflito {k}")
                    E[k] = (bits, c)
        else:
            t = tab(f"hcb_{cb}.h", f"hcb{cb}")
            def walk(o, l, c):
                leaf, d = t[o][0], t[o][1:]
                if leaf: E[ix(d)] = (l, c); return
                walk(o + d[0], l+1, c<<1); walk(o + d[1], l+1, (c<<1)|1)
            walk(0, 0, 0)
    if sorted(E) != list(range(qtd)): sys.exit(f"cb{cb}: indices {len(E)} de {qtd}")
    codes = [format(c, f'0{l}b') for l, c in E.values()]
    kraft = sum(2.0**-l for l, c in E.values())
    st = sorted(codes)
    pref = any(st[i+1].startswith(st[i]) for i in range(len(st)-1))
    print(f"cb{cb}: {qtd} codigos, kraft {kraft:.6f}, prefixo-conflito {pref}, max {max(l for l,c in E.values())}", file=sys.stderr)
    if pref: sys.exit(1)
    vals = [(E[i][0] << 24) | E[i][1] for i in range(qtd)]
    out.append(f"static const uint32_t aac_cb{cb}[{qtd}] = {{" + ",".join(("\n    " if i % 8 == 0 else " ") + hex(v) for i, v in enumerate(vals)) + "\n};")
print("\n".join(out))
