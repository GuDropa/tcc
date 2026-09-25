/*
 * rng.h — o gerador ancorado, e a chave que o ancora.
 *
 * Estagio S04. O problema que este arquivo resolve esta em §G de
 * specs/S04-rng.md: no SLEUTH original o sorteio que uma celula recebe depende
 * de quantas celulas foram processadas antes dela, porque o gerador e um fluxo
 * unico e o consumo por celula e condicional (0,134 sorteio por celula varrida
 * — so celula urbana sorteia). Reparta a varredura da fase 4 em faixas entre
 * threads e a posicao no fluxo passa a depender do escalonador. Nao e corrida
 * de dados: e dependencia de ordem, e nenhum mutex conserta.
 *
 * A saida e trocar estado por FUNCAO PURA DA CHAVE. rng_double() nao tem
 * estado, nao tem trava e nao tem armazenamento por thread: o mesmo
 * (semente, ano, fase, a, b, subpasso) da o mesmo double em qualquer ordem de
 * chamada, em qualquer thread. E isso que torna V4 — e portanto V3 — possivel.
 *
 * OS DOIS MODOS COEXISTEM (§D7 do spec do estagio):
 *
 *   --rng legacy      ran1 original, fluxo unico       -> oraculo de V7
 *   --rng anchored    contador-based ancorado a chave  -> o que S05+ usa
 *
 * Nao e indecisao. Trocar o gerador muda os numeros POR CONSTRUCAO, logo
 * quebra V7 — e V7 e o unico oraculo externo de corretude que o projeto tem,
 * justamente quando ele entra na parte arriscada (threads). Os dois modos
 * coexistem para que esse oraculo nao se perca. Ver SPEC.md §V7.
 */
#ifndef RNG_H
#define RNG_H

#include <stdint.h>

/* ------------------------------------------------------------------------ *
 * Modo de operacao
 *
 * Lidos em todo sorteio e escritos uma vez so, antes do primeiro ano. Sao
 * imutaveis durante a simulacao, que e o que permite as threads de S05 os
 * lerem sem sincronizacao — mesma garantia de g_nrows/g_ncols (globals.h).
 * ------------------------------------------------------------------------ */

#define RNG_LEGACY    0
#define RNG_ANCHORED  1

extern int      g_rng_mode;
extern uint64_t g_rng_seed;

void        rng_init (int mode, long seed);
const char *rng_mode_name (void);

/* ------------------------------------------------------------------------ *
 * A chave: (semente, ano, fase, a, b, subpasso)
 *
 * `semente` e `ano` sao globais (g_rng_seed e proc_GetCurrentYear()), entao o
 * que desce pela pilha de chamadas e so o terno (fase, a, b) — isto e, a
 * COORDENADA DETERMINISTICA DO TRABALHO. Nao precisa ser uma celula:
 *
 *   fase 4    varre a grade      -> (a,b) = (row, col) da ORIGEM
 *   fase 1n3  sorteia a celula   -> (a,b) = (k, tentativa)
 *   fase 5    sorteia da lista   -> (a,b) = (iii, tentativa)
 *
 * Ver §D4 do spec: lido ao pe da letra, "ancorado a celula" de V4 nao se
 * aplica a duas das tres fases, porque nelas nao ha celula antes do sorteio.
 * O que V4 exige e independencia de quem executa, nao uma celula.
 *
 * SOBRE O `b` SER A TENTATIVA, E NAO SO A ORIGEM. O §D8 do spec avisa que
 * spr_urbanize nao pode ancorar no ALVO, senao duas origens que tentem
 * urbanizar a mesma celula no mesmo ano recebem sorteios identicos. Verdade,
 * mas ha um caso pior e ele esta na fase 5: spread.c:397-408 chama
 * spr_urbanize_nghbr TRES VEZES COM ARGUMENTOS IDENTICOS — o laco de `tries`
 * nunca atualiza a origem. Hoje as tres tentativas diferem porque
 * spr_get_neighbor sorteia; ancorando so na origem, ficariam bit a bit iguais
 * e o laco viraria no-op silencioso. O mesmo vale para o laco de 8 tentativas
 * da fase 1n3 (spread.c:145). Por isso a regra e ancorar em
 * (fase, origem, TENTATIVA), com o indice da tentativa obrigatorio.
 * ------------------------------------------------------------------------ */

typedef struct
{
  int fase;
  int a;
  int b;
} rng_ctx_t;

#define RNG_CTX(f,x,y)  ((rng_ctx_t) { (f), (x), (y) })

/* as fases, com os numeros que o proprio modelo usa */
#define RNG_FASE_1    1   /* crescimento espontaneo                          */
#define RNG_FASE_3    3   /* novo centro difusor (o "n3" de phase1n3)        */
#define RNG_FASE_4    4   /* organico — 99,0 % dos sorteios (§D1)            */
#define RNG_FASE_5    5   /* influenciado por estradas                       */

/* ------------------------------------------------------------------------ *
 * TABELA DE SUBPASSOS (tarefa T4 do estagio)
 *
 * O subpasso distingue os varios sorteios que uma MESMA chave (fase,a,b)
 * emite. Colidir dois pontos sob a mesma chave faria as duas decisoes
 * receberem o mesmo numero — determinismo preservado, modelo corrompido, e
 * nenhum teste de "e deterministico?" acusaria.
 *
 * Conferencia de colisao, chave a chave alcancavel:
 *
 *   fase 1, (k, 0)          ROW COL BREED  +  SLOPE EXCLD   (urbanize direto)
 *   fase 3, (k, tries)      NGHBR          +  SLOPE EXCLD
 *   fase 4, (row, col)      SPREAD PIXEL   +  SLOPE EXCLD
 *   fase 5, (iii, 0)        GROWTH         +  WALK+passo
 *   fase 5, (iii, 1)        NGHBR          +  SLOPE EXCLD   (1o vizinho)
 *   fase 5, (iii, 2+tries)  NGHBR          +  SLOPE EXCLD   (laco de tries)
 *
 * Nenhuma sobreposicao.
 *
 * A CAMINHADA PRECISA DE FAIXA, NAO DE CONSTANTE. O §T4 do spec manda dar a
 * cada ponto um subpasso "distinto e constante", e para nove dos dez pontos
 * isso funciona. O decimo — o RANDOM_INT(8) de spr_road_walk (spread.c:646) —
 * e sorteado UMA VEZ POR PASSO de uma caminhada de comprimento variavel
 * (run_value escala com o coeficiente de difusao). Nao ha constante que sirva.
 * RNG_SUB_WALK e portanto uma BASE: o passo n usa RNG_SUB_WALK + n. Por isso
 * ela vale 16 e nao 9 — deixa a faixa [16, 65535] livre para a caminhada sem
 * encostar em nada. Divergencia registrada em §B do spec do estagio.
 * ------------------------------------------------------------------------ */

#define RNG_SUB_ROW      0   /* phase1n3     linha sorteada                  */
#define RNG_SUB_COL      1   /* phase1n3     coluna sorteada                 */
#define RNG_SUB_BREED    2   /* phase1n3     teste do coeficiente de breed   */
#define RNG_SUB_SPREAD   3   /* phase4       teste de espalhamento           */
#define RNG_SUB_PIXEL    4   /* phase4       qual dos 8 vizinhos             */
#define RNG_SUB_GROWTH   5   /* phase5       indice na lista de crescimento  */
#define RNG_SUB_NGHBR    6   /* get_neighbor indice inicial da varredura     */
#define RNG_SUB_SLOPE    7   /* urbanize     teste de declividade            */
#define RNG_SUB_EXCLD    8   /* urbanize     teste de area excluida          */
#define RNG_SUB_WALK    16   /* road_walk    BASE; o passo n usa WALK + n    */

/* ------------------------------------------------------------------------ *
 * A funcao pura
 *
 * Assinatura de §D8 do spec, com a semente explicita: e assim que os testes de
 * VS15/VS16 a exercitam sem montar um mundo em volta. Os pontos de sorteio do
 * spread.c nao a chamam direto — usam rng_next/rng_next_int abaixo, que
 * escolhem o modo.
 * ------------------------------------------------------------------------ */

double rng_double (uint64_t semente, int ano, int fase, int a, int b, int sub);

/* ------------------------------------------------------------------------ *
 * Os pontos de sorteio chamam estas
 *
 * Despacham entre legacy e anchored. No modo legacy a chave e inteiramente
 * ignorada e o valor vem do ran1 — e por isso que passar a chave a todo ponto
 * de sorteio nao custa nada a V7: em legacy os argumentos novos nao alteram
 * uma virgula do fluxo.
 *
 * rng_next_int mantem a forma `(int) (x * n)` das macros RANDOM_* originais
 * (random.h) de proposito: os dois modos ficam estruturalmente comparaveis, e
 * a unica diferenca entre eles e de onde vem o x.
 * ------------------------------------------------------------------------ */

double rng_next     (rng_ctx_t ctx, int sub);
int    rng_next_int (rng_ctx_t ctx, int sub, int n);

/* equivalentes de RANDOM_ROW / RANDOM_COL, que usavam g_nrows / g_ncols */
#define RNG_ROW(ctx)  rng_next_int ((ctx), RNG_SUB_ROW, g_nrows)
#define RNG_COL(ctx)  rng_next_int ((ctx), RNG_SUB_COL, g_ncols)

#endif /* RNG_H */
