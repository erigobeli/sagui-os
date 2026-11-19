# Segurança do SaguiOS

O SaguiOS é um sistema operacional escrito do zero, com kernel, drivers, rede,
TLS, SSH e navegador próprios. Isso quer dizer que os defeitos de segurança
também são nossos, e queremos saber deles. Esta página explica como contar
um defeito de segurança sem expor quem usa o sistema enquanto ele não é
corrigido.

## Como relatar uma vulnerabilidade (de forma privada)

**Não abra uma issue pública** para uma vulnerabilidade. Use o recurso
"Private vulnerability reporting" do GitHub:

1. Abra a aba **Security** do repositório
   (<https://github.com/erigobeli/sagui-os/security>).
2. Clique em **Report a vulnerability**.
3. Descreva o problema. Ajuda muito mandar:
   - a versão (a do "Sobre o SaguiOS" ou o commit do `main`);
   - onde está o defeito (arquivo e função, se souber);
   - como reproduzir: passos, o arquivo, a página ou o pacote de rede que
     dispara o problema;
   - o que acontece e o que um atacante conseguiria fazer com isso;
   - se quiser, uma ideia de correção.

O relato fica visível só para você e para o mantenedor até ser publicado.
Esse é o único canal para vulnerabilidades: não mande por e-mail, por
comentário em pull request nem por rede social.

Pode escrever em português ou em inglês.

## O que conta como vulnerabilidade

Alguns exemplos do que queremos receber por aqui:

- **Memória do kernel**: escrita ou leitura fora de um buffer no kernel ou num
  driver, uma chamada de sistema que aceita um ponteiro de um programa sem
  conferir, um programa que consegue mexer na memória de outro ou do kernel.
- **Discos**: qualquer jeito de gravar num disco (setores crus, o instalador,
  a tabela de partições) **sem a confirmação digitada** por quem usa, ou de
  gravar num disco diferente do que foi confirmado.
- **A chave da IA**: qualquer caminho que vaze a chave de API guardada no
  sistema (para a rede, para um arquivo legível por outros programas, para o
  registro, para uma captura de tela).
- **TLS e SSH**: o cliente aceitar um certificado que não devia (vencido, de
  outro nome, assinado por quem não é raiz de confiança, assinatura errada),
  aceitar uma chave de servidor SSH trocada sem avisar, decifrar dados com a
  marca de autenticação errada, ou usar números aleatórios previsíveis.
- **Arquivos e páginas maliciosos**: um arquivo (imagem, vídeo, ZIP, PDF,
  página HTML, JavaScript, JSON) que faz um programa do sistema escrever fora
  da memória dele, travar o sistema inteiro ou rodar código.
- **Rede**: um pacote que trava o kernel ou corrompe a memória dele.
- **Build e CI**: algo em `Makefile`, `tools/`, `tests/` ou `.github/` que
  execute código de fora ou exponha credenciais.

Não contam (podem ir numa issue comum): um programa que fecha com erro sem
afetar o resto do sistema, travamentos que dependem de acesso físico com o
sistema já destravado, e as limitações já documentadas (por exemplo, o TLS
não consultar revogação de certificados: veja `userland/tls/LEIA-ME.md`).
Na dúvida, mande pelo canal privado.

## Prazos (metas, sem garantia)

O SaguiOS é um projeto pessoal, mantido por voluntários no tempo livre. Os
prazos abaixo são **metas**, não um compromisso, e o programa vem **sem
nenhuma garantia**, como dizem as seções 15 e 16 da licença
([`LICENSE`](LICENSE), GPL 3).

- **Resposta**: procuramos confirmar o recebimento e dar uma primeira
  avaliação em **até 14 dias**.
- **Correção**: procuramos corrigir as falhas de gravidade **média ou alta**
  o quanto antes, de preferência em **até 60 dias** depois de confirmadas.
  Falhas graves (por exemplo, gravar no disco sem confirmação) têm prioridade
  sobre qualquer outro trabalho.
- Depois da correção, publicamos um aviso (GitHub Security Advisory) e uma
  linha no [`VERSOES.md`](VERSOES.md) dizendo o que foi corrigido. Se você
  quiser, o seu nome entra nos créditos do aviso.

## Versões que recebem correção

| Versão | Recebe correção? |
|---|---|
| A última versão publicada nas [Releases](https://github.com/erigobeli/sagui-os/releases) e o `main` | Sim |
| Versões anteriores | Não: atualize para a última |

O SaguiOS é um projeto pequeno; as correções vão para o `main` e saem na
próxima versão.

## Por favor

**Não publique detalhes da vulnerabilidade antes da correção** (nem em
issue, nem em blog, nem em rede social). Combinamos juntos a data de
publicação, depois que a correção estiver no ar. Se passarem 60 dias
sem correção, avise pelo mesmo relato antes de publicar.

## Como o projeto cuida da segurança

- Todo pull request passa por uma revisão de segurança antes de ser juntado
  (veja "Revisão de segurança" no [`CONTRIBUTING.md`](CONTRIBUTING.md)).
- O build usa `-Wall -Wextra` e compila sem avisos; o CI roda a suíte de
  testes no QEMU e o analisador estático do GCC (`-fanalyzer`). Os
  decodificadores de imagem, o DEFLATE, o leitor de ZIP, o JSON e o
  HTML também são testados no PC com AddressSanitizer e
  UndefinedBehaviorSanitizer; a criptografia é conferida com os vetores
  oficiais e contra o `openssl`.
- A decisão que criou esta página está em `docs/decisoes.md` (D074).

---

## Summary in English

To report a security vulnerability in SaguiOS, **do not open a public
issue**. Use GitHub's private vulnerability reporting: go to the
**Security** tab of this repository and click **Report a vulnerability**.
Reports in English are welcome.

We consider vulnerabilities to include, among others: out-of-bounds reads or
writes in the kernel or drivers, any way to write to a disk without the typed
user confirmation, leaks of the stored AI API key, and the TLS or SSH clients
accepting certificates, host keys or messages they should reject.

We will acknowledge your report within **14 days** and fix confirmed medium
or high severity issues within **60 days**. Only the latest release (and
`main`) receives fixes. Please **do not disclose the issue publicly before a
fix is released**; we will agree on a disclosure date with you.
