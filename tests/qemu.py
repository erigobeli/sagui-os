"""
qemu.py - controle do QEMU para os testes automáticos do SaguiOS.

Cada teste cria uma Maquina, que:
  - liga o QEMU sem janela (-display none), sem KVM;
  - manda a serial COM1 para um arquivo (o log do kernel);
  - abre o monitor do QEMU num socket Unix, usado para enviar teclas, mover o
    mouse, ler a memória e tirar capturas de tela (screendump -> PNG).

Dois modos de boot (PRD 3.3):
  "ide" - a imagem como disco IDE;
  "usb" - a imagem como pendrive num controlador EHCI (simula o G470).

Tempo máximo de qualquer espera: 3 minutos (PRD seção 7).
"""
import os
import re
import socket
import subprocess
import time

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGEM = os.path.join(RAIZ, "build", "saguios.img")
SAIDA = os.path.join(RAIZ, "build", "testes")
CAPTURAS = os.path.join(RAIZ, "docs", "capturas")
TEMPO_MAX = 180

# Como digitar cada caractere num teclado ABNT2 pelo QEMU. O QEMU nomeia as
# teclas pela posição do teclado americano; o kernel aplica o mapa ABNT2. Por
# isso o "/" é a tecla "ro" (0x73), o ";" é a tecla "slash" etc.
TECLAS = {" ": "spc", "\n": "ret", "\t": "tab", "-": "minus", "=": "equal", ".": "dot",
          ",": "comma", "/": "ro", "?": "shift-ro", ";": "slash", ":": "shift-slash",
          "'": "grave_accent", '"': "shift-grave_accent", "[": "bracket_right", "{": "shift-bracket_right",
          "]": "backslash", "}": "shift-backslash", "\\": "less", "|": "shift-less",
          "_": "shift-minus", "+": "shift-equal", ">": "shift-dot", "<": "shift-comma",
          "!": "shift-1", "@": "shift-2", "#": "shift-3", "$": "shift-4", "%": "shift-5",
          "&": "shift-7", "*": "shift-8", "(": "shift-9", ")": "shift-0"}


def ler_ppm(caminho):
    with open(caminho, "rb") as f:
        dados = f.read()
    partes = dados.split(None, 4)   # P6, largura, altura, máximo, pixels
    l, a = int(partes[1]), int(partes[2])
    return l, a, dados[len(dados) - l * a * 3:]   # os pixels são os últimos l*a*3 bytes


_FONTE = None


def _fonte():
    """bitmap de 16 bytes -> caractere, lido do C gerado da fonte."""
    global _FONTE
    if _FONTE is None:
        _FONTE = {}
        src = open(os.path.join(RAIZ, "build", "gerado", "fonte8x16.c")).read()
        linhas = [l for l in src.splitlines() if l.strip().startswith("{")]
        for codigo, l in enumerate(linhas):
            bits = bytes(int(v, 16) for v in re.findall(r"0x([0-9A-F]{2})", l.split("/*")[0]))
            if codigo >= 0x20 and bits not in _FONTE:
                _FONTE[bits] = chr(codigo)
    return _FONTE


def reconhecer_texto(larg, alt, pixels):
    fonte = _fonte()
    colunas, linhas = larg // 8, alt // 16
    tela = []
    for cy in range(linhas):
        linha = []
        for cx in range(colunas):
            cores = []
            for j in range(16):
                ini = ((cy * 16 + j) * larg + cx * 8) * 3
                cores.append([pixels[ini + 3 * i: ini + 3 * i + 3] for i in range(8)])
            fundo = cores[0][7]   # a coluna 8 de cima é sempre fundo na fonte
            frente = next((c for fila in cores for c in fila if c != fundo), None)
            if frente is None:
                linha.append(" ")
                continue
            achou = " "
            for f, b in ((frente, fundo), (fundo, frente)):   # cursor = cores trocadas
                bits = bytes(sum(0x80 >> i for i in range(8) if fila[i] == f) for fila in cores)
                if bits in fonte:
                    achou = fonte[bits]
                    break
            linha.append(achou)
        tela.append("".join(linha).rstrip())
    return tela


def cor_do_pixel(png, x, y):
    """Lê a cor de um pixel de uma captura (via ImageMagick): (r, g, b)."""
    saida = subprocess.run(["convert", png, "-crop", f"1x1+{x}+{y}", "-depth", "8", "txt:-"],
                           capture_output=True, text=True, check=True).stdout
    m = re.search(r"\((\d+),(\d+),(\d+)", saida.splitlines()[-1])
    return tuple(int(v) for v in m.groups())


def tamanho_png(png):
    saida = subprocess.run(["identify", "-format", "%w %h", png], capture_output=True, text=True, check=True).stdout
    l, a = saida.split()
    return int(l), int(a)


def imagem_de_teste(teste, extras=()):
    """Monta build/testes/teste-<nome>.img: a imagem normal com um autoteste
    do kernel ligado pelo cabeçalho de boot (campo CAB_TESTE)."""
    b = os.path.join(RAIZ, "build")
    os.makedirs(SAIDA, exist_ok=True)
    seguro = re.sub(r"[^A-Za-z0-9_.-]+", "_", teste).strip("_")[:40]
    destino = os.path.join(SAIDA, f"teste-{seguro}.img")
    subprocess.run(["python3", os.path.join(RAIZ, "tools", "mkimagem.py"),
                    "--mbr", os.path.join(b, "boot", "mbr.bin"), "--estagio2", os.path.join(b, "boot", "estagio2.bin"),
                    "--kernel", os.path.join(b, "kernel.bin"), "--kernel-elf", os.path.join(b, "kernel.elf"),
                    "--ramdisk", os.path.join(b, "ramdisk.sgfs"), "--saida", destino, "--teste", teste, *extras],
                   check=True, stdout=subprocess.DEVNULL)
    return destino


# Como digitar letras acentuadas num teclado ABNT2 pelo QEMU (que nomeia as
# teclas pela posição americana): tecla morta + letra, ou a tecla do ç.
ACENTOS_ABNT2 = {"ç": ["semicolon"], "Ç": ["shift-semicolon"]}
for _letra, _mortas in (("aeiou", [("´", "bracket_left"), ("`", "shift-bracket_left"),
                                   ("^", "shift-apostrophe"), ("¨", "shift-6")]), ("ao", [("~", "apostrophe")])):
    for _acento, _tecla in _mortas:
        for _l in _letra:
            _comb = {"´": "\u0301", "`": "\u0300", "^": "\u0302", "~": "\u0303", "¨": "\u0308"}[_acento]
            import unicodedata as _ud
            ACENTOS_ABNT2[_ud.normalize("NFC", _l + _comb)] = [_tecla, _l]
            ACENTOS_ABNT2[_ud.normalize("NFC", _l.upper() + _comb)] = [_tecla, "shift-" + _l]


def criar_pendrive(nome, mb=64, rotulo="MEUPEN", arquivos=None, pastas=(), particionado=True, formato="fat32"):
    """Cria build/testes/<nome>.img: um pendrive de teste, como o Windows
    faria (tabela MBR com uma partição FAT32 a partir de 1 MB), e copia
    arquivos e pastas para ele com o mtools. Devolve o caminho."""
    os.makedirs(SAIDA, exist_ok=True)
    img = os.path.join(SAIDA, nome + ".img")
    if os.path.exists(img):
        os.remove(img)
    with open(img, "wb") as f:
        f.truncate(mb * 1024 * 1024)
    desloc = 0
    if particionado:
        tipo = {"fat32": "c", "ntfs": "7", "ext": "83"}[formato]
        subprocess.run(["sfdisk", "-q", img], input=f"start=2048, type={tipo}\n", text=True, check=True)
        desloc = 2048
    if formato == "fat32":
        subprocess.run(["mkfs.vfat", "-F", "32", "-n", rotulo, "--offset", str(desloc), img],
                       check=True, stdout=subprocess.DEVNULL)
        for p in pastas:
            mtools(img, "mmd", f"p:/{p}", particionado=particionado)
        for destino, conteudo in (arquivos or {}).items():
            origem = os.path.join(SAIDA, "_conteudo.tmp")
            with open(origem, "wb") as f:
                f.write(conteudo.encode("utf-8") if isinstance(conteudo, str) else conteudo)
            mtools(img, "mcopy", "-o", origem, f"p:/{destino}", particionado=particionado)
    return img


def mtools(img, comando, *args, particionado=True):
    """Roda um comando do mtools no pendrive de teste (letra p:)."""
    rc = os.path.join(SAIDA, "mtoolsrc")
    with open(rc, "w") as f:
        f.write(f'drive p: file="{img}"' + (" offset=1048576" if particionado else "") + "\nmtools_skip_check=1\n")
    env = dict(os.environ, MTOOLSRC=rc, LC_ALL="C.UTF-8")
    r = subprocess.run([comando, *args], env=env, capture_output=True)
    return r.stdout.decode("utf-8", "replace"), r.returncode


def pendrive_qemu(img, ident="pen"):
    """Argumentos do QEMU para um pendrive de dados no controlador EHCI."""
    return ["-drive", f"if=none,id={ident},format=raw,file={img}",
            "-device", f"usb-storage,bus=ehci.0,drive={ident},removable=on,id=dev-{ident}"]


def imagem_console():
    """A imagem normal, mas com o terminal de tela cheia em vez da área de
    trabalho (opção --console). Usada pelos testes das fases 2 a 4."""
    return imagem_de_teste("", ["--vesa", "--console"])


class FalhaTeste(Exception):
    pass


class Maquina:
    def __init__(self, nome, modo="ide", imagem=IMAGEM, memoria="512M", extras=None, formato="raw"):
        self.nome = f"{nome}-{modo}"
        self.modo = modo
        self.imagem = imagem
        self.memoria = memoria
        self.extras = extras or []
        self.formato = formato
        os.makedirs(SAIDA, exist_ok=True)
        self.arq_serial = os.path.join(SAIDA, self.nome + ".serial")
        self.sock_monitor = os.path.join(SAIDA, self.nome + ".mon")
        self.proc = None
        self.mon = None

    # ------------------------------------------------------------ ciclo de vida
    def ligar(self):
        for f in (self.arq_serial, self.sock_monitor):
            if os.path.exists(f):
                os.remove(f)
        cmd = ["qemu-system-i386", "-m", self.memoria, "-display", "none",
               "-serial", "file:" + self.arq_serial,
               "-monitor", f"unix:{self.sock_monitor},server,nowait",
               "-no-reboot"]
        if self.modo == "ide":
            cmd += ["-drive", f"format={self.formato},file={self.imagem},if=ide,index=0,snapshot=on"]
        elif self.modo == "usb":
            cmd += ["-device", "usb-ehci,id=ehci",
                    "-drive", f"if=none,id=pendrive,format={self.formato},file={self.imagem},snapshot=on",
                    "-device", "usb-storage,bus=ehci.0,drive=pendrive,bootindex=0"]
        else:
            raise ValueError(self.modo)
        cmd += self.extras
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        for _ in range(100):
            if os.path.exists(self.sock_monitor):
                break
            time.sleep(0.05)
        self.mon = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.mon.connect(self.sock_monitor)
        self.mon.settimeout(10)
        self._ler_monitor()  # banner do monitor
        return self

    def desligar(self):
        try:
            if self.mon:
                self.mon.sendall(b"quit\n")
                self.mon.close()
        except OSError:
            pass
        if self.proc:
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()

    def __enter__(self):
        return self.ligar()

    def __exit__(self, *a):
        self.desligar()

    # ------------------------------------------------------------ monitor
    def _ler_monitor(self):
        dados = b""
        while not dados.endswith(b"(qemu) "):
            parte = self.mon.recv(65536)
            if not parte:
                break
            dados += parte
        return dados.decode("utf-8", "replace")

    def monitor(self, comando):
        self.mon.sendall(comando.encode() + b"\n")
        resp = self._ler_monitor()
        # Remove o eco do comando e o prompt; tira sequências de terminal.
        resp = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", resp)
        linhas = resp.replace("\r", "").split("\n")
        return "\n".join(l for l in linhas[1:] if not l.startswith("(qemu)"))

    # ------------------------------------------------------------ serial
    def serial(self):
        if not os.path.exists(self.arq_serial):
            return ""
        with open(self.arq_serial, "rb") as f:
            return f.read().decode("latin-1")

    def esperar_serial(self, texto, tempo=TEMPO_MAX):
        fim = time.time() + tempo
        while time.time() < fim:
            if texto in self.serial():
                return True
            if self.proc.poll() is not None:
                raise FalhaTeste(f"QEMU terminou antes de aparecer {texto!r}")
            time.sleep(0.2)
        raise FalhaTeste(f"tempo esgotado esperando {texto!r} na serial")

    # ------------------------------------------------------------ tela
    def tela_texto(self):
        """Lê o texto da tela. No modo texto VGA, direto da memória (0xB8000).
        No modo gráfico (fase 4 em diante), reconhecendo cada célula de 8x16
        da captura pelo bitmap da fonte do próprio SaguiOS."""
        ppm = os.path.join(SAIDA, self.nome + "-tela.ppm")
        self.monitor(f"screendump {ppm}")
        for _ in range(50):
            if os.path.exists(ppm) and os.path.getsize(ppm) > 0:
                break
            time.sleep(0.1)
        time.sleep(0.1)
        l, a, pixels = ler_ppm(ppm)
        if (l, a) != (720, 400):
            return reconhecer_texto(l, a, pixels)
        saida = self.monitor("xp /2000hx 0xb8000")
        celulas = []
        for linha in saida.splitlines():
            if ":" not in linha:
                continue
            for palavra in linha.split(":", 1)[1].split():
                celulas.append(int(palavra, 16))
        chars = "".join(chr(c & 0xFF) if 32 <= (c & 0xFF) < 127 else " " for c in celulas[:2000])
        return [chars[i:i + 80].rstrip() for i in range(0, len(chars), 80)]

    def esperar_tela(self, texto, tempo=60):
        fim = time.time() + tempo
        while time.time() < fim:
            if any(texto in l for l in self.tela_texto()):
                return True
            time.sleep(0.3)
        raise FalhaTeste(f"tempo esgotado esperando {texto!r} na tela")

    def captura(self, nome, guardar=True):
        """Tira uma captura de tela. guardar=True salva em docs/capturas/."""
        ppm = os.path.join(SAIDA, self.nome + "-" + nome + ".ppm")
        destino = os.path.join(CAPTURAS if guardar else SAIDA, nome + ".png")
        self.monitor(f"screendump {ppm}")
        for _ in range(50):
            if os.path.exists(ppm) and os.path.getsize(ppm) > 0:
                break
            time.sleep(0.1)
        time.sleep(0.2)
        try:
            subprocess.run(["convert", ppm, destino], check=True)
        except Exception:
            from PIL import Image
            Image.open(ppm).save(destino)
        os.remove(ppm)
        return destino

    # ------------------------------------------------------------ shell
    def comando(self, texto, tempo=60, teclas_finais=("ret",)):
        """Digita um comando no sagsh, aperta Enter e espera o próximo prompt.
        Devolve o que saiu na serial durante o comando (sem '\r')."""
        antes = self.serial()
        n = antes.count("sagui:")
        self.digitar(texto, 0.04)
        for t in teclas_finais:
            self.tecla(t, 0.04)
        fim = time.time() + tempo
        while self.serial().count("sagui:") <= n:
            if time.time() > fim:
                raise FalhaTeste(f"o comando {texto!r} não voltou ao prompt")
            if self.proc.poll() is not None:
                break
            time.sleep(0.2)
        return self.serial()[len(antes):].replace("\r", "")

    # ------------------------------------------------------------ mouse
    def mouse_para(self, x, y):
        """Leva o cursor a (x, y): primeiro ao canto (0,0), depois em passos."""
        # Passos pequenos e com pausa: o QEMU junta movimentos seguidos, e um
        # pacote que estoura o limite do PS/2 é descartado pelo driver.
        for _ in range(14):
            self.monitor("mouse_move -80 -80")
            time.sleep(0.02)
        self._mx = self._my = 0
        self.mouse_mover(x, y)

    def mouse_mover(self, x, y, botao_apertado=False):
        """Move em passos pequenos até (x, y) a partir da posição conhecida."""
        while (self._mx, self._my) != (x, y):
            dx = max(-40, min(40, x - self._mx))
            dy = max(-40, min(40, y - self._my))
            self.monitor(f"mouse_move {dx} {dy}")
            self._mx += dx
            self._my += dy
            time.sleep(0.03)
        time.sleep(0.2)

    def clique(self, x, y, botao=1, duplo=False):
        self.mouse_para(x, y)
        for _ in range(2 if duplo else 1):
            self.monitor(f"mouse_button {botao}")
            time.sleep(0.05)
            self.monitor("mouse_button 0")
            time.sleep(0.1)
        time.sleep(0.4)

    def arrastar(self, x0, y0, x1, y1):
        self.mouse_para(x0, y0)
        self.monitor("mouse_button 1")
        time.sleep(0.2)
        self.mouse_mover(x1, y1)
        self.monitor("mouse_button 0")
        time.sleep(0.5)

    # ------------------------------------------------------------ teclado
    def tecla(self, nome, espera=0.08):
        self.monitor(f"sendkey {nome}")
        time.sleep(espera)

    def digitar(self, texto, espera=0.08):
        for c in texto:
            if c in ACENTOS_ABNT2:
                for t in ACENTOS_ABNT2[c]:
                    self.tecla(t, espera)
            elif c in TECLAS:
                self.tecla(TECLAS[c], espera)
            elif c.isupper():
                self.tecla("shift-" + c.lower(), espera)
            else:
                self.tecla(c, espera)


def verificar(condicao, mensagem):
    if not condicao:
        raise FalhaTeste(mensagem)
    print("  ok:", mensagem)
