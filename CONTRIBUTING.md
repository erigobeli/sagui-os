# Como contribuir com o SaguiOS

Obrigado pelo interesse! O SaguiOS começou como um sistema operacional escrito
do zero pelo Claude (Anthropic) para o Eli Rigobeli e hoje aceita
contribuições da comunidade, inclusive feitas com a ajuda de IA. Para o projeto
continuar sendo "escrito do zero" e com a licença em ordem, há algumas regras,
inspiradas nas do kernel do Linux e bem mais simples.

## 1. Escreva do zero, a partir de especificações públicas

- **Não copie nem adapte código de outros sistemas operacionais ou projetos**
  (Linux, BSDs, drivers, pilhas de rede, TLS, navegadores, compiladores...).
- **Pode** ler manuais, datasheets, RFCs e especificações públicas e escrever o
  seu código a partir deles. Cite o documento no comentário do arquivo.
- **O núcleo é sempre do zero**: kernel, drivers, sistema de arquivos, rede,
  TLS, janelas, `libsagui`, shell, compilador e programas básicos não usam
  nenhum código de terceiros.
- **Nos aplicativos** (navegador, tocadores, jogos...), do zero é o padrão.
  Código livre de terceiros só entra como exceção, se a licença for compatível
  com a GPL 3 (MIT, BSD, Apache 2.0, LGPL, GPL 2 "ou posterior", GPL 3, domínio
  público; nunca GPL 2 "somente" nem código fechado), numa pasta própria em
  `terceiros/`, com a licença original, listado em `CREDITOS.md` e dito
  claramente no pull request. Detalhes em [`AGENTS.md`](AGENTS.md) e na
  decisão D041 de `docs/decisoes.md`.
- As ferramentas de build e de teste (Python, GCC, QEMU) ficam fora dessa regra.
- Usou IA? Ótimo, mas a responsabilidade pelo que é enviado é sua. As regras
  para a IA estão em [`AGENTS.md`](AGENTS.md) (muitas ferramentas leem esse
  arquivo sozinhas).

## 2. Assine os commits (DCO)

Cada commit precisa da linha:

```
Signed-off-by: Seu Nome <seu@email>
```

(`git commit -s` coloca sozinho.) Com ela, você declara, como no
[Developer Certificate of Origin](https://developercertificate.org/), que
escreveu o código ou tem o direito de enviá-lo, e que ele pode ser distribuído
sob a GPL 3 (a licença do projeto, em [`LICENSE`](LICENSE)).

## 3. Pull requests pequenos e testados

- **Um assunto por pull request**. Coisas grandes: quebre em partes que dê
  para revisar (por exemplo, "motor de JavaScript" e "Canvas" separados).
- Baseie o trabalho no **`main` atual** (a versão 2.0 mudou muita coisa desde
  a 1.0).
- `make` precisa compilar e `python3 tests/rodar_todos.py` precisa passar.
  Funcionalidade nova vem com teste novo em `tests/`.
- Código, comentários e documentação em **português**, no estilo dos arquivos
  vizinhos.

## 4. Como funciona a revisão

O Eli é o mantenedor. A revisão é feita com a ajuda do Claude: ele lê o pull
request, confere as regras acima (inclusive sinais de código copiado), roda os
testes e passa um resumo para o Eli decidir. Às vezes, em vez de juntar o pull
request como está, a ideia é trazida para o código atual com o seu nome como
coautor (`Co-authored-by`), o que aparece no seu perfil do GitHub como
contribuição.

## 5. Ideias e problemas

Não quer programar? **Issues** com ideias, relatos de erro (com foto da tela ou
a saída do comando `registro`) e testes em computadores diferentes ajudam
muito.

Quem contribuiu está em [`CONTRIBUIDORES.md`](CONTRIBUIDORES.md).
