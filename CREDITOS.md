# Créditos: o que vem de fora dentro do SaguiOS

O **núcleo** do SaguiOS (kernel, drivers, sistema de arquivos, rede, TLS, SSH, janelas,
`libsagui`, shell, compilador e programas básicos) foi todo escrito neste projeto (D041).
Dentro da imagem, o que veio de fora está listado aqui: **dados públicos** (os certificados
raiz, usados pelo HTTPS para conferir que um site é quem diz ser), a **imagem de fundo** da
Mata Atlântica e, nos aplicativos, o código livre de terceiros da pasta `terceiros/`.

## Fundo "Mata Atlântica" (`/arte/mata-atlantica.sgi`)

- **Origem:** imagem gerada por inteligência artificial (modelo Z-Image Turbo) e trazida por
  Robson de Lima Barbosa no pull request #1; convertida para o formato `.sgi` por
  `tools/mkfundo.py`. Aprovada pelo Eli (H81).
- **Licença:** distribuída com o SaguiOS sob a GPL 3, como o resto da arte do projeto.

## Certificados raiz (`/sistema/certificados/`)

- **Origem:** a lista de autoridades certificadoras da Mozilla (NSS), na forma em que o Ubuntu a
  distribui no pacote `ca-certificates` (versão 20260816ubuntu0.24.04.2), copiada por `tools/mkraizes.py` em
  2026-11-13.
- **Licença:** Mozilla Public License 2.0 (permite redistribuir; compatível com a GPL 3 do SaguiOS).
- **Quantidade:** 24 raízes, 23777 bytes. Só as que cobrem os sites mais usados e os
  serviços de que o SaguiOS precisa; a lista completa tem mais de 100.

| Arquivo | Para quê | Certificado |
| --- | --- | --- |
| `isrg-x1.der` | Let's Encrypt (RSA): a maior parte dos sites, inclusive a Wikipédia | subject=C = US, O = Internet Security Research Group, CN = ISRG Root X1; notAfter=Jun  4 11:04:38 2035 GMT |
| `isrg-x2.der` | Let's Encrypt (curva elíptica) | subject=C = US, O = Internet Security Research Group, CN = ISRG Root X2; notAfter=Sep 17 16:00:00 2040 GMT |
| `gts-r1.der` | Google Trust Services (Google, Cloudflare) | subject=C = US, O = Google Trust Services LLC, CN = GTS Root R1; notAfter=Jun 22 00:00:00 2036 GMT |
| `gts-r3.der` | Google Trust Services | subject=C = US, O = Google Trust Services LLC, CN = GTS Root R3; notAfter=Jun 22 00:00:00 2036 GMT |
| `gts-r4.der` | Google Trust Services (curva elíptica: Cloudflare, OpenRouter) | subject=C = US, O = Google Trust Services LLC, CN = GTS Root R4; notAfter=Jun 22 00:00:00 2036 GMT |
| `digicert-global-g2.der` | DigiCert (GitHub, Microsoft e muitos outros) | subject=C = US, O = DigiCert Inc, OU = www.digicert.com, CN = DigiCert Global Root G2; notAfter=Jan 15 12:00:00 2038 GMT |
| `digicert-global-g3.der` | DigiCert (curva elíptica) | subject=C = US, O = DigiCert Inc, OU = www.digicert.com, CN = DigiCert Global Root G3; notAfter=Jan 15 12:00:00 2038 GMT |
| `digicert-assured-id-g2.der` | DigiCert | subject=C = US, O = DigiCert Inc, OU = www.digicert.com, CN = DigiCert Assured ID Root G2; notAfter=Jan 15 12:00:00 2038 GMT |
| `globalsign-raiz-r3.der` | GlobalSign | subject=OU = GlobalSign Root CA - R3, O = GlobalSign, CN = GlobalSign; notAfter=Mar 18 10:00:00 2029 GMT |
| `globalsign-r46.der` | GlobalSign | subject=C = BE, O = GlobalSign nv-sa, CN = GlobalSign Root R46; notAfter=Mar 20 00:00:00 2046 GMT |
| `globalsign-e46.der` | GlobalSign (curva elíptica) | subject=C = BE, O = GlobalSign nv-sa, CN = GlobalSign Root E46; notAfter=Mar 20 00:00:00 2046 GMT |
| `usertrust-rsa-ca.der` | Sectigo/USERTrust (muitos sites) | subject=C = US, ST = New Jersey, L = Jersey City, O = The USERTRUST Network, CN = USERTrust RSA Certification Authority; notAfter=Jan 18 23:59:59 2038 GMT |
| `usertrust-ecc-ca.der` | Sectigo/USERTrust (curva elíptica) | subject=C = US, ST = New Jersey, L = Jersey City, O = The USERTRUST Network, CN = USERTrust ECC Certification Authority; notAfter=Jan 18 23:59:59 2038 GMT |
| `comodo-rsa-ca.der` | Sectigo (Comodo) | subject=C = GB, ST = Greater Manchester, L = Salford, O = COMODO CA Limited, CN = COMODO RSA Certification Authority; notAfter=Jan 18 23:59:59 2038 GMT |
| `sectigo-servidor-r46.der` | Sectigo | subject=C = GB, O = Sectigo Limited, CN = Sectigo Public Server Authentication Root R46; notAfter=Mar 21 23:59:59 2046 GMT |
| `amazon-raiz-1.der` | Amazon (sites na AWS) | subject=C = US, O = Amazon, CN = Amazon Root CA 1; notAfter=Jan 17 00:00:00 2038 GMT |
| `amazon-raiz-3.der` | Amazon (curva elíptica) | subject=C = US, O = Amazon, CN = Amazon Root CA 3; notAfter=May 26 00:00:00 2040 GMT |
| `microsoft-rsa-2017.der` | Microsoft | subject=C = US, O = Microsoft Corporation, CN = Microsoft RSA Root Certificate Authority 2017; notAfter=Jul 18 23:00:23 2042 GMT |
| `sslcom-raiz-rsa.der` | SSL.com | subject=C = US, ST = Texas, L = Houston, O = SSL Corporation, CN = SSL.com Root Certification Authority RSA; notAfter=Feb 12 17:39:39 2041 GMT |
| `sslcom-tls-ecc-raiz-20.der` | SSL.com 2022 (curva elíptica: Cloudflare, inclusive example.com) | subject=C = US, O = SSL Corporation, CN = SSL.com TLS ECC Root CA 2022; notAfter=Aug 19 16:33:47 2046 GMT |
| `sslcom-tls-rsa-raiz-20.der` | SSL.com 2022 (Cloudflare) | subject=C = US, O = SSL Corporation, CN = SSL.com TLS RSA Root CA 2022; notAfter=Aug 19 16:34:21 2046 GMT |
| `certum-trusted-network.der` | Certum | subject=C = PL, O = Unizeto Technologies S.A., OU = Certum Certification Authority, CN = Certum Trusted Network CA; notAfter=Dec 31 12:07:37 2029 GMT |
| `go-daddy-raiz-g2.der` | GoDaddy | subject=C = US, ST = Arizona, L = Scottsdale, O = "GoDaddy.com, Inc.", CN = Go Daddy Root Certificate Authority - G2; notAfter=Dec 31 23:59:59 2037 GMT |
| `starfield-raiz-g2.der` | Starfield | subject=C = US, ST = Arizona, L = Scottsdale, O = "Starfield Technologies, Inc.", CN = Starfield Root Certificate Authority - G2; notAfter=Dec 31 23:59:59 2037 GMT |
