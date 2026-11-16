#!/usr/bin/env python3
"""H105 - discos rígidos IDE/ATA (issue #45, parte 1).

Critérios:
  - o driver ATA acha o disco do sistema (hd0) e um segundo disco (hd1);
  - um disco com a FAT32 do SaguiOS (.saguios/configuracoes.txt) monta
    sozinho em /usb, sem o aviso de "pendrive";
  - o SaguiOS lê um arquivo que foi gravado nele aqui fora;
  - um arquivo gravado pelo SaguiOS chega ao disco (conferido aqui fora com o
    mtools, depois de desmontar) e a FAT32 continua íntegra (fsck.vfat);
  - captura para a revisão a olho (o ícone do disco se chama "Disco").
"""
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu import Maquina, verificar, criar_pendrive, mtools, SAIDA  # noqa: E402


def main():
    hd = criar_pendrive("h105-hd", 128, "SAGUIOS", pastas=(".saguios",), arquivos={
        ".saguios/configuracoes.txt": b"aparencia.efeitos = automatico\n",
        "nota.txt": "escrito aqui fora\n".encode(),
    })
    extras = ["-drive", f"format=raw,file={hd},if=ide,index=1"]
    with Maquina("h105", "ide", extras=extras, memoria="768M") as m:
        m.esperar_serial("sagui:/$", 120)
        s = m.serial()
        verificar("[DISCO] hd0:" in s and "[DISCO] hd1:" in s, "o driver ATA acha os dois discos (hd0 e hd1)")
        verificar("tem configuracoes do SaguiOS: montado sozinho" in s, "a FAT32 do SaguiOS no hd1 monta sozinha em /usb")
        verificar("Pendrive com as suas configura" not in s, "sem aviso de pendrive para o disco interno")
        out = m.comando("cat /usb/nota.txt")
        verificar("escrito aqui fora" in out, "o SaguiOS lê o arquivo gravado aqui fora")
        m.comando("echo gravado pelo sagui > /usb/novo.txt")
        time.sleep(1)
        m.captura("H105-disco")
        out = m.comando("desmontar")
        verificar("Disco hd1 desmontado: tudo gravado." in out and "pendrive" not in out,
                  f"desmontar fala de disco, não de pendrive ({out.strip()[-60:]!r})")

    local = os.path.join(SAIDA, "h105-novo.txt")
    mtools(hd, "mcopy", "-o", "p:/novo.txt", local)
    verificar(open(local, "rb").read().strip() == b"gravado pelo sagui", "o arquivo gravado pelo SaguiOS está no disco")
    fat = os.path.join(SAIDA, "h105-fat.img")
    with open(hd, "rb") as f, open(fat, "wb") as g:
        f.seek(2048 * 512)
        g.write(f.read())
    r = subprocess.run(["fsck.vfat", "-n", fat], capture_output=True, text=True)
    os.remove(fat)
    verificar(r.returncode == 0, f"fsck.vfat: a FAT32 continua íntegra ({r.stdout.strip().splitlines()[-1]})")


if __name__ == "__main__":
    main()
