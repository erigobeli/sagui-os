#!/usr/bin/env python3
"""H51: números do JS em IEEE 754 (dupla precisão): aritmética, texto mais curto,
leitura correta, toFixed/toPrecision/toExponential exatos, toString(raiz), Math,
typed arrays de ponto flutuante, Date.now real. Esperados conferidos no V8 e no
Python (mesmo IEEE 754)."""
import functools
import http.server
import os
import re
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, SAIDA  # noqa: E402

CASOS = [
    ("soma", "0.1+0.2", "0.30000000000000004"), ("terco", "1/3", "0.3333333333333333"), ("mul", "4.35*100", "434.99999999999994"),
    ("p53", "2**53", "9007199254740992"), ("p53b", "2**53+1", "9007199254740992"), ("lit", "9007199254740993", "9007199254740992"),
    ("e21", "1e21", "1e+21"), ("e20", "1e20", "100000000000000000000"), ("em7", "1e-7", "1e-7"), ("em6", "0.000001", "0.000001"),
    ("e18", "123e-20", "1.23e-18"), ("max", "Number.MAX_VALUE", "1.7976931348623157e+308"), ("min", "Number.MIN_VALUE", "5e-324"),
    ("inf", "1e400", "Infinity"), ("ovf", "Number.MAX_VALUE*10", "Infinity"), ("negz", "String(-0)", "0"), ("divz", "1/-0", "-Infinity"),
    ("hex", "(255).toString(16)", "ff"), ("bin", "(0.5).toString(2)", "0.1"), ("hexf", "(-255.5).toString(16)", "-ff.8"),
    ("pi16", "Math.PI.toString(16)", "3.243f6a8885a3"), ("b36", "(25).toString(36)", "p"),
    ("fix1", "(1.005).toFixed(2)", "1.00"), ("fix2", "(1.45).toFixed(1)", "1.4"), ("fix3", "(123.456).toFixed(10)", "123.4560000000"),
    ("fix4", "(0).toFixed(2)", "0.00"), ("fix5", "(1e21).toFixed(2)", "1e+21"), ("fix6", "(-1.5).toFixed(0)", "-2"), ("fix7", "(0.5).toFixed(0)", "1"),
    ("pre1", "(123.456).toPrecision(4)", "123.5"), ("pre2", "(0.000123).toPrecision(2)", "0.00012"), ("pre3", "(123456).toPrecision(2)", "1.2e+5"),
    ("exp1", "(123.456).toExponential(2)", "1.23e+2"), ("exp2", "(0).toExponential()", "0e+0"), ("exp3", "(12345).toExponential()", "1.2345e+4"),
    ("pf", "parseFloat('3.14abc')", "3.14"), ("pi1", "parseInt('0x1F')", "31"), ("pi2", "parseInt('123456789012345678901234567890')", "1.2345678901234568e+29"),
    ("pi3", "parseInt('zz',36)", "1295"), ("n1", "Number('  12  ')", "12"), ("n2", "Number('1_000')", "NaN"), ("n3", "Number('0b101')", "5"),
    ("n4", "Number('')", "0"), ("n5", "Number('.5e1')", "5"), ("n6", "1_000_000", "1000000"),
    ("sin", "Math.sin(Math.PI/6)", "0.49999999999999994"), ("sqrt2", "Math.pow(2,0.5)===Math.SQRT2", "true"), ("e", "Math.exp(1)===Math.E", "true"),
    ("ln", "Math.log(Math.E)", "1"), ("l10", "Math.log10(1000)", "3"), ("cbrt", "Math.cbrt(27)", "3"), ("hyp", "Math.hypot(3,4)", "5"),
    ("rnd", "Math.round(-2.5)", "-2"), ("rndz", "Object.is(Math.round(-0.4),-0)", "true"), ("maxz", "Object.is(Math.max(-0,0),0)", "true"),
    ("at2", "Math.atan2(1,1)*4===Math.PI", "true"), ("f32", "new Float32Array([1.1])[0]", "1.100000023841858"),
    ("f64", "new Float64Array([0.1])[0]===0.1", "true"), ("clamp", "String(new Uint8ClampedArray([1.5,2.5,300,-5]))", "2,2,255,0"),
    ("i32a", "~~(2**32+5)", "5"), ("i32b", "(2**31)|0", "-2147483648"), ("u32", "-1>>>0", "4294967295"),
    ("iso", "new Date(0).toISOString()", "1970-01-01T00:00:00.000Z"), ("utc", "Date.UTC(2026,10,8)", "1794096000000"),
    ("agora", "Date.now()>1.7e12", "true"), ("json", "JSON.stringify([1e21,0.1,-0])", "[1e+21,0.1,0]"),
    ("mod", "5.5%2", "1.5"), ("modn", "-5%2", "-1"), ("modz", "Object.is(-4%2,-0)", "true"), ("pow", "(-8)**(1/3)", "NaN"),
]


def main():
    linhas = "".join('try { console.log("R %s=" + String(%s)); } catch (e) { console.log("R %s=ERRO " + e); }\n' % (k, x, k) for k, x, _ in CASOS)
    pasta = os.path.join(SAIDA, "h51-www")
    os.makedirs(pasta, exist_ok=True)
    with open(os.path.join(pasta, "index.html"), "w") as f:
        f.write("<html><head><title>Numeros</title></head><body><script>\n" + linhas + 'console.log("RES fim");</script></body></html>')
    trat = functools.partial(http.server.SimpleHTTPRequestHandler, directory=pasta)
    trat.log_message = lambda *a: None
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), trat)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
    img = imagem_de_teste(f"/bin/cipo http://10.0.2.2:{srv.server_address[1]}/", ["--vesa"])
    with Maquina("h51", "ide", imagem=img, extras=extras) as m:
        m.esperar_serial("js: RES fim", 90)
        s = m.serial()
        got = dict(l.strip().partition("=")[::2] for l in re.findall(r"js: R (.*)", s))
        ruins = [(k, x, got.get(k), v) for k, x, v in CASOS if got.get(k) != v]
        for k, x, g, v in ruins:
            print(f"  DIFERE {k}: {x} -> {g!r}, esperado {v!r}")
        print(f"casos: {len(CASOS) - len(ruins)}/{len(CASOS)}")
        verificar(not ruins, "números do JS iguais ao V8 (IEEE 754)")
    srv.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
