#!/usr/bin/env python3
"""Copia para o SaguiOS os certificados raiz escolhidos (H54).

Os certificados raiz são dados públicos: a "lista de autoridades confiáveis"
que todo navegador e sistema carrega. A origem é a lista da Mozilla (o
Ubuntu a distribui no pacote ca-certificates, em /etc/ssl/certs), sob a
licença MPL 2.0, que permite redistribuir e é compatível com a GPL 3. Só
entram as raízes que cobrem os sites mais usados e os serviços de que o
SaguiOS precisa (Wikipédia, GitHub, OpenRouter, Cloudflare, Google...).

Uso: tools/mkraizes.py  (grava rootfs/sistema/certificados/*.der e CREDITOS.md)"""
import base64
import datetime
import os
import subprocess

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORIGEM = "/etc/ssl/certs"
DESTINO = os.path.join(RAIZ, "rootfs", "sistema", "certificados")
ESCOLHIDAS = [
    ("ISRG_Root_X1", "Let's Encrypt (RSA): a maior parte dos sites, inclusive a Wikipédia"),
    ("ISRG_Root_X2", "Let's Encrypt (curva elíptica)"),
    ("GTS_Root_R1", "Google Trust Services (Google, Cloudflare)"),
    ("GTS_Root_R3", "Google Trust Services"),
    ("GTS_Root_R4", "Google Trust Services (curva elíptica: Cloudflare, OpenRouter)"),
    ("DigiCert_Global_Root_G2", "DigiCert (GitHub, Microsoft e muitos outros)"),
    ("DigiCert_Global_Root_G3", "DigiCert (curva elíptica)"),
    ("DigiCert_Assured_ID_Root_G2", "DigiCert"),
    ("GlobalSign_Root_CA_-_R3", "GlobalSign"),
    ("GlobalSign_Root_R46", "GlobalSign"),
    ("GlobalSign_Root_E46", "GlobalSign (curva elíptica)"),
    ("USERTrust_RSA_Certification_Authority", "Sectigo/USERTrust (muitos sites)"),
    ("USERTrust_ECC_Certification_Authority", "Sectigo/USERTrust (curva elíptica)"),
    ("COMODO_RSA_Certification_Authority", "Sectigo (Comodo)"),
    ("Sectigo_Public_Server_Authentication_Root_R46", "Sectigo"),
    ("Amazon_Root_CA_1", "Amazon (sites na AWS)"),
    ("Amazon_Root_CA_3", "Amazon (curva elíptica)"),
    ("Microsoft_RSA_Root_Certificate_Authority_2017", "Microsoft"),
    ("SSL.com_Root_Certification_Authority_RSA", "SSL.com"),
    ("SSL.com_TLS_ECC_Root_CA_2022", "SSL.com 2022 (curva elíptica: Cloudflare, inclusive example.com)"),
    ("SSL.com_TLS_RSA_Root_CA_2022", "SSL.com 2022 (Cloudflare)"),
    ("Certum_Trusted_Network_CA", "Certum"),
    ("Go_Daddy_Root_Certificate_Authority_-_G2", "GoDaddy"),
    ("Starfield_Root_Certificate_Authority_-_G2", "Starfield"),
]


def pem_para_der(texto):
    corpo = texto.split("-----BEGIN CERTIFICATE-----")[1].split("-----END CERTIFICATE-----")[0]
    return base64.b64decode("".join(corpo.split()))


def main():
    os.makedirs(DESTINO, exist_ok=True)
    for nome in os.listdir(DESTINO):
        os.remove(os.path.join(DESTINO, nome))
    linhas = []
    total = 0
    for nome, uso in ESCOLHIDAS:
        pem = os.path.join(ORIGEM, nome + ".pem")
        der = pem_para_der(open(pem).read())
        # O SGFS guarda caminhos de até 47 letras: "sistema/certificados/" + nome curto.
        curto = nome.lower().replace("_-_", "-").replace("_", "-").replace(".", "")
        for longo, breve in (("certification-authority", "ca"), ("certificate-authority", "ca"),
                             ("public-server-authentication", "servidor"), ("root-certificate-authority", "raiz"),
                             ("root-ca", "raiz"), ("-root", ""), ("microsoft-rsa-raiz-2017", "microsoft-rsa-2017")):
            curto = curto.replace(longo, breve)
        curto = curto[:22]
        with open(os.path.join(DESTINO, curto + ".der"), "wb") as f:
            f.write(der)
        total += len(der)
        assunto = subprocess.run(["openssl", "x509", "-in", pem, "-noout", "-subject", "-enddate"],
                                 capture_output=True, text=True).stdout.strip().replace("\n", "; ")
        linhas.append(f"| `{curto}.der` | {uso} | {assunto} |")
    versao = subprocess.run(["dpkg-query", "-W", "-f=${Version}", "ca-certificates"], capture_output=True, text=True).stdout
    with open(os.path.join(RAIZ, "CREDITOS.md"), "w", encoding="utf-8") as f:
        f.write("# Créditos: o que vem de fora dentro do SaguiOS\n\n")
        f.write("Todo o **código** do SaguiOS foi escrito neste projeto. Dentro da imagem, a única coisa feita\n")
        f.write("por terceiros são **dados públicos**: os certificados raiz abaixo, usados pelo HTTPS (H53) para\n")
        f.write("conferir que um site é quem diz ser.\n\n")
        f.write("## Certificados raiz (`/sistema/certificados/`)\n\n")
        f.write("- **Origem:** a lista de autoridades certificadoras da Mozilla (NSS), na forma em que o Ubuntu a\n")
        f.write(f"  distribui no pacote `ca-certificates` (versão {versao}), copiada por `tools/mkraizes.py` em\n")
        f.write(f"  {datetime.date.today().isoformat()}.\n")
        f.write("- **Licença:** Mozilla Public License 2.0 (permite redistribuir; compatível com a GPL 3 do SaguiOS).\n")
        f.write(f"- **Quantidade:** {len(ESCOLHIDAS)} raízes, {total} bytes. Só as que cobrem os sites mais usados e os\n")
        f.write("  serviços de que o SaguiOS precisa; a lista completa tem mais de 100.\n\n")
        f.write("| Arquivo | Para quê | Certificado |\n| --- | --- | --- |\n")
        f.write("\n".join(linhas) + "\n")
    print(f"mkraizes: {len(ESCOLHIDAS)} raízes, {total} bytes -> {DESTINO}")


if __name__ == "__main__":
    main()
