# Respostas para o selo OpenSSF Best Practices (nível "passing")

Este arquivo prepara as respostas do SaguiOS para o formulário do selo
[OpenSSF Best Practices](https://www.bestpractices.dev) (sugestão da issue
#17; decisão D074 em [`decisoes.md`](decisoes.md)). Os critérios são os 67
do nível "passing", na ordem do site
(<https://www.bestpractices.dev/en/criteria/0>).

Como usar: o mantenedor entra no site com a conta do GitHub, cadastra o
repositório e copia daqui, critério por critério, a resposta (**Met**,
**Unmet** ou **N/A**) e a justificativa. O formulário é em inglês, por isso
a justificativa para colar também está em inglês; a explicação em português
vem antes de cada tabela, quando precisa.

**Honestidade.** As respostas são declarações públicas do projeto. Nada aqui
foi marcado Met sem estar feito; onde a resposta depende de algo que o
mantenedor ainda precisa ligar no GitHub, isso está dito.

Antes de preencher, o mantenedor precisa:

1. ligar o **Private vulnerability reporting** (Settings > Code security >
   Private vulnerability reporting > Enable). Sem isso, o
   `vulnerability_report_private` e o `SECURITY.md` ficam sem efeito;
2. conferir que o job "Análise estática (gcc -fanalyzer)" rodou no `main`
   (aba Actions) depois do merge da D074.

Atalho dos links: `R` = `https://github.com/erigobeli/sagui-os/blob/main`.

## Como o SaguiOS é desenvolvido (contexto para vários critérios)

O código do SaguiOS é escrito por IA (o Claude, da Anthropic) sob a direção
do mantenedor, Eli Rigobeli, que decide o que entra, testa no hardware real
e junta os pull requests. Contribuições da comunidade chegam por pull
request. A segurança se apoia em:

- **revisão de segurança em todo pull request**, antes da revisão técnica,
  com uma lista fixa de 9 itens (abaixo; o resumo público está em
  [`CONTRIBUTING.md`](../CONTRIBUTING.md), seção "Revisão de segurança");
- **testes**: mais de cem testes automáticos no QEMU (`tests/`), e os
  decodificadores de imagem do navegador (GIF, WebP e os usados pelo motor), o DEFLATE, o leitor de ZIP,
  o JSON e o HTML compilados também no PC com AddressSanitizer e
  UndefinedBehaviorSanitizer (`h61`, `h69`, `h82`, `h93`, `h96`);
- **CI** no GitHub Actions em todo pull request, com o build sem avisos e o
  analisador estático do GCC (`-fanalyzer`).

A lista de 9 itens da revisão de segurança:

1. **Workflows e CI** (`.github/`): permissão mínima (`contents: read`),
   nenhum `pull_request_target`, nenhum segredo, só ações oficiais, nada
   baixado da internet e executado, nada que publique fora.
2. **Código que roda no build e nos testes** (`Makefile`, `tools/`,
   `tests/`): nenhuma rede inesperada, nenhum comando estranho, nada escrito
   fora do repositório e de `build/`.
3. **Binários e arquivos opacos**: de onde vêm e por que são necessários;
   executáveis prontos nunca entram.
4. **Núcleo**: nada grava em disco sem a confirmação digitada; as checagens
   de ponteiros vindos dos programas não são afrouxadas; procurar escrita fora
   de buffers, contas que dão a volta em 32 bits, laços sem limite.
5. **Segredos e dados pessoais** em código, capturas, logs ou commits.
6. **Origem e licença** (D041): o núcleo é do zero; código de terceiros só
   nos aplicativos, em `terceiros/`, com licença compatível com a GPL 3.
7. **Drivers**: só os escritos a partir de manuais e especificações públicas,
   citados no arquivo.
8. **Responsabilidade**: quem responde pelo pull request é quem o envia e
   assina os commits (`Signed-off-by`, DCO).
9. **Ler antes de rodar**: o código de um pull request só é executado depois
   de lido.

## Básico

| Critério | Resposta | Justificativa (para colar) |
|---|---|---|
| `description_good` | Met | The README explains what SaguiOS is: a 32-bit x86 operating system written from scratch (kernel, drivers, network stack, TLS, SSH, browser, C compiler), runnable on QEMU, VirtualBox and real hardware. R/README.md |
| `interact` | Met | The README explains how to obtain it (GitHub Releases, build from source), how to give feedback (GitHub issues) and how to contribute (CONTRIBUTING.md). R/README.md R/CONTRIBUTING.md |
| `contribution` | Met | Contributions are made through GitHub pull requests, as described in CONTRIBUTING.md. R/CONTRIBUTING.md |
| `contribution_requirements` | Met | CONTRIBUTING.md and AGENTS.md list the requirements: code written from public specifications, DCO sign-off, small PRs, tests for new features, Portuguese naming, no compiler warnings, and a pull request template. R/CONTRIBUTING.md R/AGENTS.md |
| `floss_license` | Met | GPL-3.0. R/LICENSE |
| `floss_license_osi` | Met | GPL-3.0 is OSI-approved. Third-party code in terceiros/ uses OSI-approved, GPL-3-compatible licenses (MIT, BSD, Apache 2.0, LGPL 2.1+, GPL 2+). R/CREDITOS.md |
| `license_location` | Met | R/LICENSE at the repository root. |
| `documentation_basics` | Met | README (overview, how to run and build), docs/como-gravar.md (how to install), docs/ci-cd.md, and a README per library (e.g. userland/tls/LEIA-ME.md). |
| `documentation_interface` | Met | The system call ABI is documented in kernel/inc/abi.h and the programs' library in userland/inc/sagui.h and gui.h; shell commands have built-in help (`ajuda`); the TLS, SSH and crypto libraries document their APIs in their LEIA-ME.md files. |
| `sites_https` | Met | The project site is the GitHub repository; repository, releases and downloads are served over HTTPS. |
| `discussion` | Met | GitHub issues are used for proposals and discussion (e.g. #25, #45, #51); they are searchable, URL-addressable and open to anyone without proprietary client software. |
| `english` | **Unmet** | Documentation and code comments are in Brazilian Portuguese on purpose: the project is meant to be readable by Brazilian beginners. Bug reports, vulnerability reports and pull requests in English are accepted and answered (see the English summary in SECURITY.md). R/SECURITY.md |
| `maintained` | Met | Actively maintained: frequent commits, releases (v2.1, v2.2.0 test builds) and pull requests merged weekly. |

## Controle de mudanças

| Critério | Resposta | Justificativa (para colar) |
|---|---|---|
| `repo_public` | Met | https://github.com/erigobeli/sagui-os |
| `repo_track` | Met | Git records every change, author and date; contributors sign commits with Signed-off-by (DCO). |
| `repo_interim` | Met | All work happens on public branches and pull requests between releases. |
| `repo_distributed` | Met | Git. |
| `version_unique` | Met | Each release has a unique version (1.0, 2.1, 2.2.0...) shown in the "About SaguiOS" window. R/VERSOES.md |
| `version_semver` | Met | MAJOR.MINOR.PATCH, explained in VERSOES.md. R/VERSOES.md |
| `version_tags` | Met | Releases are git tags (v2.1, v2.2.0-teste1...). |
| `release_notes` | Met | VERSOES.md is the human-readable changelog, also shipped inside the system; each GitHub release has notes. R/VERSOES.md |
| `release_notes_vulns` | N/A | No publicly known vulnerability has been fixed yet. SECURITY.md commits to listing fixed vulnerabilities in VERSOES.md and in a GitHub Security Advisory. |

## Relatos

O `report_responses` e o `enhancement_responses` contam issues de pessoas
de fora; hoje todas tiveram resposta, menos a #17 (que este trabalho
responde). Antes de marcar, conferir de novo em
<https://github.com/erigobeli/sagui-os/issues?q=is%3Aissue>.

| Critério | Resposta | Justificativa (para colar) |
|---|---|---|
| `report_process` | Met | Bugs are reported through GitHub issues, as explained in CONTRIBUTING.md (section 5). |
| `report_tracker` | Met | GitHub issues. |
| `report_responses` | Met | The maintainer answers bug reports in the issues (e.g. #8, #9). |
| `enhancement_responses` | Met | Enhancement proposals are answered in the issues (e.g. #25, #45, #51). |
| `report_archive` | Met | GitHub issues are public and searchable. |
| `vulnerability_report_process` | Met | SECURITY.md, linked from README and CONTRIBUTING. R/SECURITY.md |
| `vulnerability_report_private` | Met | Private reports through GitHub private vulnerability reporting (Security tab > Report a vulnerability), as described in SECURITY.md. *(Só marcar depois de ligar o recurso.)* |
| `vulnerability_report_response` | N/A | No vulnerability reports received in the last 6 months. SECURITY.md commits to an initial response within 14 days. |

## Qualidade

| Critério | Resposta | Justificativa (para colar) |
|---|---|---|
| `build` | Met | `make` rebuilds the whole disk image from source. R/Makefile |
| `build_common_tools` | Met | GNU make, GCC, NASM, binutils and Python 3. |
| `build_floss_tools` | Met | All build tools are FLOSS (GCC, NASM, binutils, make, Python, mtools). |
| `test` | Met | More than 100 automated tests in tests/, run with `python3 tests/rodar_todos.py` or `make test`, documented in README ("Compilar e testar"). |
| `test_invocation` | Met | `make test`. |
| `test_most` | **Unmet** | The suite exercises most features end to end in QEMU (boot, memory, windows, USB, network, TLS, SSH, browser, compiler), but branch coverage is not measured, so we do not claim it covers most branches. |
| `test_continuous_integration` | Met | GitHub Actions builds and runs the full suite on every pull request and push to main. R/.github/workflows/ci.yml |
| `test_policy` | Met | "New functionality comes with a new test" is a written rule in CONTRIBUTING.md, AGENTS.md and the pull request template. |
| `tests_are_added` | Met | Recent features came with tests (e.g. h110 installer, h112 partition table, h117 du). |
| `tests_documented_added` | Met | The pull request template asks for tests for what is new. R/.github/pull_request_template.md |
| `warnings` | Met | The kernel and programs are built with `-Wall -Wextra -Werror=implicit-function-declaration`. R/Makefile |
| `warnings_fixed` | Met | The build compiles with zero warnings (decision D074 fixed the last ones); CONTRIBUTING.md asks to keep it that way. |
| `warnings_strict` | Met | `-Wall -Wextra` on all project code, plus GCC's static analyzer in CI. Stricter flags such as -Wconversion are not used because of the volume of intentional narrowing in pixel and codec code. |

## Segurança: conhecimento de desenvolvimento seguro

Estes dois critérios pedem que "pelo menos um desenvolvedor principal" saiba
projetar software seguro e conheça os erros comuns. A resposta precisa ser
honesta sobre como o projeto funciona: o código é escrito por IA sob a
direção do mantenedor. A justificativa diz isso com todas as letras e aponta
o que existe de concreto (a lista de revisão, os testes com sanitizers, as
decisões de segurança escritas).

| Critério | Resposta | Justificativa (para colar) |
|---|---|---|
| `know_secure_design` | Met | SaguiOS code is written by an AI model (Claude, Anthropic) under the direction of the maintainer, who reviews and tests every change. Secure-design principles are applied and documented: system calls check pointers coming from programs before using them, raw disk writes are locked in the kernel behind a typed user confirmation (D063), TLS 1.3 only with full certificate chain validation, SSH host keys pinned on first use. Every PR goes through a written 9-item security review (docs/openssf.md, CONTRIBUTING.md). |
| `know_common_errors` | Met | The security review explicitly looks for the common C errors (out-of-bounds writes, 32-bit integer wraparound, unbounded loops, unchecked user pointers); decoders, DEFLATE, ZIP, JSON and HTML are tested with AddressSanitizer/UBSan, including malformed-input tests (h96); GCC -fanalyzer runs in CI. |

## Segurança: criptografia

O SaguiOS escreve a própria criptografia **de propósito**: o núcleo é do zero
por regra do projeto (D041), e isso inclui `userland/cripto`, o TLS e o SSH.
Por isso `crypto_call` fica Unmet, com justificativa.

O `crypto_keylength` está **Unmet** hoje: a conferência de assinaturas RSA
aceita módulos a partir de 1024 bits (`userland/cripto/numeros.c`,
`rsa_abrir`), e o NIST pede 2048 até 2030. Subir o mínimo para 2048 bits é
uma mudança de uma linha (com teste) e torna o critério Met; fica proposta
para um pull request próprio.

| Critério | Resposta | Justificativa (para colar) |
|---|---|---|
| `crypto_published` | Met | Only published, reviewed algorithms and protocols: TLS 1.3 (RFC 8446), SSH 2 (RFC 4251-4254), X25519/Ed25519 (RFC 7748/8032), AES-GCM, ChaCha20-Poly1305 (RFC 8439), SHA-2, HKDF, RSA PKCS#1 v1.5/PSS, ECDSA P-256/P-384. R/userland/cripto/LEIA-ME.md |
| `crypto_call` | **Unmet** | Deliberate: the project rule (decision D041) is that the core of SaguiOS, including cryptography, TLS and SSH, is written from scratch from the public specifications. The code is checked against official test vectors and against OpenSSL/OpenSSH in the test suite, and the documentation warns that it is not audited. |
| `crypto_floss` | Met | All cryptography is our own GPL-3 code. |
| `crypto_keylength` | **Unmet** | Symmetric keys are 128/256 bits and ECC uses X25519, P-256 and P-384, but RSA signature verification still accepts moduli from 1024 bits. Raising the minimum to 2048 is planned. |
| `crypto_working` | Met | No broken algorithms: TLS 1.3 only (no TLS 1.2 or older, no RC4, no CBC, no SHA-1 signatures); SSH uses curve25519-sha256, aes-ctr, hmac-sha2-256 and rsa-sha2-256/ssh-ed25519. |
| `crypto_weaknesses` | Met | Defaults avoid algorithms and modes with known serious weaknesses (see crypto_working). |
| `crypto_pfs` | Met | TLS 1.3 and SSH key exchange use ephemeral X25519. |
| `crypto_password_storage` | N/A | SaguiOS does not store user passwords (there are no user accounts; the SSH client sends a typed password to the server and does not keep it). |
| `crypto_random` | Met | Keys and nonces come from the kernel CSPRNG (kernel/nucleo/aleatorio.c, decision D035): entropy from interrupt timing (RDTSC) and RDRAND when present, mixed into a pool that keys a ChaCha20 generator with key erasure after each request. |

## Segurança: entrega, vulnerabilidades e credenciais

| Critério | Resposta | Justificativa (para colar) |
|---|---|---|
| `delivery_mitm` | Met | Releases and source are delivered only over HTTPS from GitHub. |
| `delivery_unsigned` | Met | SHA-256 files are published next to the images on the same HTTPS GitHub release; nothing is retrieved over HTTP. |
| `vulnerabilities_fixed_60_days` | Met | There are no publicly known unpatched vulnerabilities. SECURITY.md commits to fixing medium and higher severity issues within 60 days. |
| `vulnerabilities_critical_fixed` | Met | SECURITY.md gives critical issues priority over any other work. |
| `no_leaked_credentials` | Met | No credentials in the repository; the AI API key is typed by the user inside the system and never committed. CI uses no secrets. |

## Análise

O `static_analysis_fixed` e o `dynamic_analysis_fixed` pedem que achados de
gravidade média ou alta, depois de confirmados, sejam corrigidos logo. Os
achados do `-fanalyzer` da primeira rodada estão descritos na D074.

| Critério | Resposta | Justificativa (para colar) |
|---|---|---|
| `static_analysis` | Met | GCC's static analyzer (-fanalyzer) runs in CI on every push and pull request over all project C code (kernel/ and userland/). R/tools/analise_estatica.py R/.github/workflows/ci.yml |
| `static_analysis_common_vulnerabilities` | Met | -fanalyzer looks for common C vulnerabilities: NULL dereference, use after free, double free, use of uninitialized values, out-of-bounds access, leaks. |
| `static_analysis_fixed` | Met | Confirmed analyzer findings are fixed; see decision D074 in docs/decisoes.md. The CI job is informational for now and will be made blocking once the remaining findings are triaged. |
| `static_analysis_often` | Met | On every push and pull request. |
| `dynamic_analysis` | Met | Image decoders (GIF, WebP), the layout engine, DEFLATE, ZIP, JSON and HTML are built on the host with AddressSanitizer and UndefinedBehaviorSanitizer and exercised by the test suite on every CI run. |
| `dynamic_analysis_unsafe` | Met | SaguiOS is written in C (memory-unsafe); ASan/UBSan tests run in CI, including malformed-input tests (h96). The kernel itself runs only in QEMU tests, without sanitizers. |
| `dynamic_analysis_enable_assertions` | Met | The sanitizer builds run with -fno-sanitize-recover, so every ASan/UBSan check acts as an assertion that fails the test. |
| `dynamic_analysis_fixed` | Met | Issues found by the sanitizer tests are fixed before merging (CI must pass). |

## Critérios Unmet (resumo)

| Critério | Nível | Por quê |
|---|---|---|
| `english` | SHOULD | Documentação em português de propósito; relatos em inglês são aceitos. |
| `crypto_call` | SHOULD | Criptografia própria de propósito (D041). |
| `crypto_keylength` | MUST | RSA aceita 1024 bits na conferência; subir para 2048 resolve. |
| `test_most` | SUGGESTED | Cobertura de ramos não é medida. |

Com o `crypto_keylength` Unmet (é MUST), o selo "passing" ainda não sai:
o site só dá o selo com todos os MUST em Met ou N/A. Corrigido o mínimo do
RSA, os demais Unmet são SHOULD/SUGGESTED com justificativa, o que o selo
aceita.
