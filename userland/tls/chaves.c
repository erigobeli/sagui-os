/* chaves.c - de onde saem as chaves do TLS 1.3 (RFC 8446, seção 7).
 *
 * Tudo sai de segredos encadeados pelo HKDF. Cada segredo vira outros
 * através de uma única função, a HKDF-Expand-Label, que é o HKDF-Expand com
 * um "info" montado assim:
 *
 *     2 bytes: quantos bytes se quer
 *     1 byte de tamanho + "tls13 " + o rótulo
 *     1 byte de tamanho + o contexto (um resumo do que foi conversado, ou nada)
 *
 * O rótulo separa os usos ("key", "iv", "finished", "c hs traffic"...) e o
 * contexto amarra a chave à conversa: quem mexer em qualquer byte do aperto
 * de mão faz os dois lados chegarem a chaves diferentes. */
#include "tls_interno.h"

void tls_expandir_rotulo(int resumo, const u8 *segredo, const char *rotulo,
                         const u8 *contexto, u32 ncontexto, u8 *saida, u32 n)
{
    static const char prefixo[] = "tls13 ";
    u8 info[2 + 1 + 6 + 24 + 1 + RESUMO_TAM_MAX];
    u32 nr = txt_tamanho(rotulo), pos = 0;

    if (nr > 24) nr = 24;                   /* os rótulos do protocolo têm no máximo 12 letras */
    if (ncontexto > RESUMO_TAM_MAX) ncontexto = RESUMO_TAM_MAX;
    info[pos++] = (u8)(n >> 8);
    info[pos++] = (u8)n;
    info[pos++] = (u8)(6 + nr);
    for (u32 i = 0; i < 6; i++) info[pos++] = (u8)prefixo[i];
    for (u32 i = 0; i < nr; i++) info[pos++] = (u8)rotulo[i];
    info[pos++] = (u8)ncontexto;
    for (u32 i = 0; i < ncontexto; i++) info[pos++] = contexto[i];
    hkdf_expandir(resumo, segredo, resumo_tamanho(resumo), info, pos, saida, n);
}

void tls_sentido_armar(Tls *t, TlsSentido *s, const u8 *segredo)
{
    cri_copiar(s->segredo, segredo, t->tam_resumo);
    tls_expandir_rotulo(t->resumo, segredo, "key", 0, 0, s->chave, t->tam_chave);
    tls_expandir_rotulo(t->resumo, segredo, "iv", 0, 0, s->iv, 12);
    /* A chave do AES é expandida uma vez só, aqui, e não a cada registro. */
    if (t->conjunto != 0x1303) aes_gcm_iniciar(&s->gcm, s->chave, t->tam_chave);
    s->sequencia = 0;
    s->ativa = 1;
}

void tls_sentido_avancar(Tls *t, TlsSentido *s)
{
    u8 novo[RESUMO_TAM_MAX];
    tls_expandir_rotulo(t->resumo, s->segredo, "traffic upd", 0, 0, novo, t->tam_resumo);
    tls_sentido_armar(t, s, novo);
    cri_zerar(novo, sizeof novo);
}
