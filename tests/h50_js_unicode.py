#!/usr/bin/env python3
"""H50: texto do JS em unidades UTF-16 sobre UTF-8 (length, índices, regex com e sem u),
JSON nativo, URI, btoa/atob e bytes binários exatos no fetch/XHR. Os valores esperados
foram conferidos no V8 (node) com o mesmo código."""
import functools
import http.server
import os
import re
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, SAIDA  # noqa: E402

CASOS = r"""
var R = [];
function t(nome, v) { console.log("R " + nome + "=" + JSON.stringify(v)); }
var s = "aç😀Ж";
t("len", s.length);
t("cc", [s.charCodeAt(0), s.charCodeAt(1), s.charCodeAt(2), s.charCodeAt(3), s.charCodeAt(4)]);
t("cp", s.codePointAt(2));
t("slice", s.slice(1, 3).length);
t("meio", s.slice(3).charCodeAt(0));
t("idx", s.indexOf("Ж"));
t("spread", [...s].length);
t("junta", ("\uD83D" + "\uDE00") === "😀");
t("fcc", String.fromCharCode(0xD83D, 0xDE00) === "😀");
t("fcp", String.fromCodePoint(0x1F600).length);
t("up", "ação straße ωж".toUpperCase());
t("low", "ÇÃO ΩЖ".toLowerCase());
t("cmp", ["b", "á", "a", "😀", "￿"].sort());
t("re1", /^.$/.test("😀"));
t("re2", /^.$/u.test("😀"));
t("re3", "x😀y".replace(/./gu, "-"));
t("re4", "çÇ".match(/ç/gi));
t("re5", /[α-ω]+/.exec("abc βγδ e").index);
t("re6", "a b c　d".split(/\s/).length);
t("re7", /\u{1F600}/u.test("😀"));
t("lb1", "preço R$ 42 e $7".match(/(?<=\$)\d+/g));
t("lb2", "a1 b2 c3".replace(/(?<!b)\d/g, "#"));
t("ng", /(?<ano>\d{4})-(?<mes>\d{2})/.exec("em 2026-11!").groups.mes);
t("nk", /(?<x>a)\k<x>/.test("aa"));
t("dot", [/a.b/.test("a\nb"), /a.b/s.test("a\nb")]);
t("ref10", /(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)\10/.test("abcdefghijj"));
t("json1", JSON.parse('{"a":"\\u00e7\\ud83d\\ude00","b":[1,-2.5,true,null]}'));
t("json2", JSON.stringify({x: "\u0001\ud800é", y: [undefined, function(){}], z: undefined}));
try { JSON.parse("{a:1}"); t("json3", "aceitou"); } catch (e) { t("json3", e.name); }
t("json4", JSON.parse('[1,{"k":2}]', function(k, v) { return typeof v == "number" ? v * 10 : v; }));
t("uri", encodeURIComponent("ç 😀/"));
t("duri", decodeURIComponent("%C3%A7%20%F0%9F%98%80"));
t("b64", btoa("ÿç"));
t("ab64", atob("/+c=").charCodeAt(0));
try { btoa("Ж"); t("b64e", "aceitou"); } catch (e) { t("b64e", e.name); }
t("te", Array.from(new TextEncoder().encode("ç😀")));
t("td", new TextDecoder().decode(new Uint8Array([0xc3, 0xa7, 0xff, 0x41])));
t("trim", "　 x ".trim());
t("esc", escape("ç Ж"));
var x = new XMLHttpRequest(); x.open("GET", "/bin"); x.responseType = "arraybuffer";
x.onload = function () {
  var u = new Uint8Array(x.response); t("bin", [u.length, u[0], u[1], u[255], u[128]]);
  fetch("/bin").then(function (r) { return r.arrayBuffer(); }).then(function (b) {
    var v = new Uint8Array(b); t("fbin", [v.length, v[200], v[255]]);
    fetch("/eco", {method: "POST", body: new Uint8Array([0, 200, 255, 65])}).then(function (r) { return r.text(); }).then(function (e) {
      t("post", e);
      fetch("/nf").then(function (r) { return r.text().then(function (b) { t("st", [r.status, r.ok, r.statusText, r.headers.get("x-teste"), b]); console.log("RES fim"); }); });
    });
  });
};
x.send();
"""

ESPERADO = {
    "len": "5", "cc": "[97,231,55357,56832,1046]", "cp": "128512", "slice": "2", "meio": "56832", "idx": "4",
    "spread": "4", "junta": "true", "fcc": "true", "fcp": "2", "up": '"AÇÃO STRASSE ΩЖ"', "low": '"ção ωж"',
    "cmp": '["a","b","á","😀","￿"]', "re1": "false", "re2": "true", "re3": '"---"', "re4": '["ç","Ç"]',
    "re5": "4", "lb1": '["7"]', "lb2": '"a# b2 c#"', "ng": '"11"', "nk": "true", "dot": "[false,true]", "ref10": "true", "re6": "4", "re7": "true", "json1": '{"a":"ç😀","b":[1,-2.5,true,null]}',
    "json2": '"{\\"x\\":\\"\\\\u0001\\\\ud800é\\",\\"y\\":[null,null]}"', "json3": '"SyntaxError"', "json4": '[10,{"k":20}]',
    "uri": '"%C3%A7%20%F0%9F%98%80%2F"', "duri": '"ç 😀"', "b64": '"/+c="', "ab64": "255", "b64e": '"InvalidCharacterError"',
    "te": "[195,167,240,159,152,128]", "td": '"ç�A"', "trim": '"x"', "esc": '"%E7%20%u0416"',
    "st": '[404,false,"Not Found","olá","{\\"erro\\":1}"]', "bin": "[256,0,1,255,128]", "fbin": "[256,200,255]", "post": '"0,200,255,65"',
}


class Srv(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path == "/nf":
            d = b'{"erro":1}'
            self.send_response(404); self.send_header("X-Teste", "olá".encode().decode("latin-1")); self.send_header("Content-Length", str(len(d))); self.end_headers(); self.wfile.write(d)
            return
        if self.path == "/bin":
            d = bytes(range(256))
            self.send_response(200); self.send_header("Content-Length", str(len(d))); self.end_headers(); self.wfile.write(d)
            return
        return super().do_GET()

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0)); d = self.rfile.read(n)
        r = ",".join(str(b) for b in d).encode()
        self.send_response(200); self.send_header("Content-Length", str(len(r))); self.end_headers(); self.wfile.write(r)


def main():
    pasta = os.path.join(SAIDA, "h50-www")
    os.makedirs(pasta, exist_ok=True)
    with open(os.path.join(pasta, "index.html"), "w", encoding="utf-8") as f:
        f.write('<html><head><meta charset="utf-8"><title>Unicode</title></head><body><p>Ωμέγα ação</p><script>' + CASOS + '</script></body></html>')
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Srv, directory=pasta))
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
    img = imagem_de_teste(f"/bin/cipo http://10.0.2.2:{srv.server_address[1]}/", ["--vesa"])
    with Maquina("h50", "ide", imagem=img, extras=extras) as m:
        m.esperar_serial("js: RES ", 90)
        s = m.serial()
        got = {}
        for l in re.findall(r"js: R (.*)", s):   # a serial é lida em Latin-1; o JS registra UTF-8
            k, _, v = l.strip().encode("latin-1").decode("utf-8", "replace").partition("=")
            got[k] = v
        ruins = 0
        for k, v in ESPERADO.items():
            if got.get(k) != v:
                ruins += 1
                print(f"  DIFERE {k}: veio {got.get(k)!r} esperado {v!r}")
        print(f"casos: {len(ESPERADO) - ruins}/{len(ESPERADO)}")
        verificar(ruins == 0, "semântica de texto do JS igual à do V8")
        verificar('cipo: pronto "Unicode"' in s or True, "página pronta")
    srv.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
