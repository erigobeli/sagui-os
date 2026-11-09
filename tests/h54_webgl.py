#!/usr/bin/env python3
"""H54: WebGL 1 em software no Cipó.

A página monta shaders GLSL ES de verdade (funções com return, laços com
break, struct em array de uniforms, sampler) e confere o resultado com
readPixels: varyings interpolados, profundidade, culling, blending, textura
com mipmap, framebuffer com textura, drawElements com Uint16Array e o
caminho de getActiveUniform/getUniformLocation que bibliotecas como o
three.js usam. Por fim o canvas tem que aparecer na tela com a cor certa.
"""
import functools
import http.server
import os
import re
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, imagem_de_teste, SAIDA  # noqa: E402
from PIL import Image  # noqa: E402

HTML = r"""<!DOCTYPE html><html><head><meta charset="utf-8"><title>WebGL</title>
<style>body{margin:0;background:#fff}canvas{display:block;margin:20px}</style></head><body>
<canvas id="c" width="64" height="64"></canvas>
<script>
var gl=document.getElementById('c').getContext('webgl');
function R(n,ok,x){console.log('webgl: '+(ok?'ok ':'FALHA ')+n+(x!==undefined?' '+x:''))}
function sh(t,s){var x=gl.createShader(t);gl.shaderSource(x,s);gl.compileShader(x);if(!gl.getShaderParameter(x,gl.COMPILE_STATUS))throw new Error(gl.getShaderInfoLog(x));return x}
function prog(v,f){var p=gl.createProgram();gl.attachShader(p,sh(gl.VERTEX_SHADER,v));gl.attachShader(p,sh(gl.FRAGMENT_SHADER,f));gl.linkProgram(p);if(!gl.getProgramParameter(p,gl.LINK_STATUS))throw new Error(gl.getProgramInfoLog(p));gl.useProgram(p);return p}
function px(x,y){var d=new Uint8Array(4);gl.readPixels(x,y,1,1,gl.RGBA,gl.UNSIGNED_BYTE,d);return d}
function perto(d,r,g,b){return Math.abs(d[0]-r)<4&&Math.abs(d[1]-g)<4&&Math.abs(d[2]-b)<4}
var VS='attribute vec2 p;varying vec2 uv;void main(){uv=p*0.5+0.5;gl_Position=vec4(p,0.0,1.0);}';
var Q=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,Q);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([-1,-1,1,-1,-1,1,1,1]),gl.STATIC_DRAW);
function quad(p){var l=gl.getAttribLocation(p,'p');gl.bindBuffer(gl.ARRAY_BUFFER,Q);gl.enableVertexAttribArray(l);gl.vertexAttribPointer(l,2,gl.FLOAT,false,0,0);gl.drawArrays(gl.TRIANGLE_STRIP,0,4)}
try{
 // 1. GLSL: struct em array de uniforms, função com return, laço com break
 var p=prog(VS,'precision mediump float;struct L{vec3 cor;float k;};uniform L luzes[2];varying vec2 uv;'+
  'float f(float x){if(x>0.5)return 1.0;return 0.0;}'+
  'void main(){float s=0.0;for(int i=0;i<10;i++){if(i==3)break;s+=0.25;}vec3 c=luzes[0].cor*luzes[0].k+luzes[1].cor*luzes[1].k;gl_FragColor=vec4(c.r,s,f(uv.x),1.0);}');
 var n=gl.getProgramParameter(p,gl.ACTIVE_UNIFORMS),nomes=[];for(var i=0;i<n;i++)nomes.push(gl.getActiveUniform(p,i).name);
 R('uniforms ativos',nomes.indexOf('luzes[1].cor')>=0&&nomes.indexOf('luzes[0].k')>=0,nomes.join(','));
 gl.uniform3f(gl.getUniformLocation(p,'luzes[0].cor'),1,0,0);gl.uniform1f(gl.getUniformLocation(p,'luzes[0].k'),0.5);
 gl.uniform3fv(gl.getUniformLocation(p,'luzes[1].cor'),[0.5,0,0]);gl.uniform1f(gl.getUniformLocation(p,'luzes[1].k'),1);
 quad(p);var a=px(5,30),b=px(60,30);R('glsl struct/laço/função',perto(a,255,191,0)&&perto(b,255,191,255),a.join(',')+' '+b.join(','));
 // 2. varyings, profundidade e culling com drawElements
 var p2=prog('attribute vec3 p;attribute vec3 c;varying vec3 vc;void main(){vc=c;gl_Position=vec4(p,1.0);}','precision mediump float;varying vec3 vc;void main(){gl_FragColor=vec4(vc,1.0);}');
 var V=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,V);gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([-1,-1,0.5,0,1,0, 1,-1,0.5,0,1,0, -1,1,0.5,0,1,0, 1,1,0.5,0,1,0, -1,-1,-0.5,1,0,0, 1,-1,-0.5,1,0,0, -1,1,-0.5,1,0,0, 1,1,-0.5,1,0,0]),gl.STATIC_DRAW);
 var I=gl.createBuffer();gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER,I);gl.bufferData(gl.ELEMENT_ARRAY_BUFFER,new Uint16Array([4,5,6,6,5,7, 0,1,2,2,1,3]),gl.STATIC_DRAW);
 var lp=gl.getAttribLocation(p2,'p'),lc=gl.getAttribLocation(p2,'c');gl.enableVertexAttribArray(lp);gl.enableVertexAttribArray(lc);
 gl.vertexAttribPointer(lp,3,gl.FLOAT,false,24,0);gl.vertexAttribPointer(lc,3,gl.FLOAT,false,24,12);
 gl.enable(gl.DEPTH_TEST);gl.clearColor(0,0,0,1);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
 gl.drawElements(gl.TRIANGLES,12,gl.UNSIGNED_SHORT,0);a=px(32,32);R('profundidade',perto(a,255,0,0),a.join(','));
 gl.enable(gl.CULL_FACE);gl.cullFace(gl.FRONT);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);gl.drawElements(gl.TRIANGLES,12,gl.UNSIGNED_SHORT,0);
 a=px(32,32);R('culling',perto(a,0,0,0),a.join(','));gl.disable(gl.CULL_FACE);gl.disable(gl.DEPTH_TEST);
 gl.disableVertexAttribArray(lc);
 // 3. textura com mipmap + framebuffer
 var t=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,t);var d=new Uint8Array(4*4*4);for(var k=0;k<16;k++){d[k*4]=k%4<2?255:0;d[k*4+2]=k%4<2?0:255;d[k*4+3]=255}
 gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,4,4,0,gl.RGBA,gl.UNSIGNED_BYTE,d);gl.generateMipmap(gl.TEXTURE_2D);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);
 var p3=prog(VS,'precision mediump float;uniform sampler2D t;varying vec2 uv;void main(){gl_FragColor=texture2D(t,uv);}');
 var t2=gl.createTexture();gl.bindTexture(gl.TEXTURE_2D,t2);gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,64,64,0,gl.RGBA,gl.UNSIGNED_BYTE,null);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.LINEAR);
 var fb=gl.createFramebuffer();gl.bindFramebuffer(gl.FRAMEBUFFER,fb);gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,t2,0);
 R('framebuffer completo',gl.checkFramebufferStatus(gl.FRAMEBUFFER)==gl.FRAMEBUFFER_COMPLETE);
 gl.bindTexture(gl.TEXTURE_2D,t);quad(p3);gl.bindFramebuffer(gl.FRAMEBUFFER,null);
 gl.bindTexture(gl.TEXTURE_2D,t2);quad(p3);a=px(5,5);b=px(60,5);R('textura via framebuffer',perto(a,255,0,0)&&perto(b,0,0,255),a.join(',')+' '+b.join(','));
 // 4. blending
 var p4=prog(VS,'precision mediump float;uniform vec4 cor;void main(){gl_FragColor=cor;}');
 gl.clearColor(1,1,1,1);gl.clear(gl.COLOR_BUFFER_BIT);gl.enable(gl.BLEND);gl.blendFunc(gl.SRC_ALPHA,gl.ONE_MINUS_SRC_ALPHA);
 gl.uniform4f(gl.getUniformLocation(p4,'cor'),0,0,1,0.5);quad(p4);a=px(32,32);R('blending',perto(a,128,128,255),a.join(','));
 gl.disable(gl.BLEND);gl.uniform4f(gl.getUniformLocation(p4,'cor'),0,0.8,0,1);quad(p4);
 R('sem erro gl',gl.getError()==0);
}catch(e){R('excecao',false,e.message)}
</script></body></html>"""


def main():
    pasta = os.path.join(SAIDA, "h54-www")
    os.makedirs(pasta, exist_ok=True)
    with open(os.path.join(pasta, "index.html"), "w", encoding="utf-8") as f:
        f.write(HTML)
    tratador = functools.partial(http.server.SimpleHTTPRequestHandler, directory=pasta)
    tratador.log_message = lambda *a: None
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), tratador)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    extras = ["-netdev", "user,id=n0", "-device", "e1000,netdev=n0"]
    img = imagem_de_teste(f"/bin/cipo http://10.0.2.2:{srv.server_address[1]}/", ["--vesa"])
    with Maquina("h54", "ide", imagem=img, extras=extras) as m:
        m.esperar_serial('cipo: pronto "WebGL"', 120)
        s = m.serial()
        res = re.findall(r"webgl: (ok|FALHA) ([^\n]*)", s)
        for r, n in res:
            print(r, n)
        verificar(len(res) == 8, f"8 verificações rodaram ({len(res)})")
        for r, n in res:
            verificar(r == "ok", f"webgl: {n}")
        jan = re.search(r'criada por \d+: "Cip.*?" (\d+)x(\d+) em \((\d+),(\d+)\)', s)
        jl, ja, jx, jy = (int(x) for x in jan.groups())
        ox, oy = jx + 2, jy + 24 + 40
        im = Image.open(m.captura("h54", guardar=False)).convert("RGB")
        verdes = sum(1 for x in range(ox + 20, ox + 84, 4) for y in range(oy + 20, oy + 84, 4) if im.getpixel((x, y)) == (0, 204, 0))
        print("pixels verdes do canvas", verdes)
        verificar(verdes > 200, "o canvas WebGL aparece na página com a última cor desenhada")


if __name__ == "__main__":
    main()
