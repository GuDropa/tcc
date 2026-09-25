/*
 * rng.c — implementacao do gerador ancorado e do despacho entre os dois modos.
 *
 * Ver rng.h para a chave, a tabela de subpassos e o porque dos dois modos.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "globals.h"
#include "rng.h"

int      g_rng_mode = RNG_ANCHORED;
uint64_t g_rng_seed = 1;

/* ------------------------------------------------------------------------ *
 * O misturador
 *
 * Finalizador do SplitMix64 (Steele, Lea & Flood, 2014): duas multiplicacoes
 * por constantes impares e tres xorshifts. E uma bijecao em 64 bits com
 * avalanche completa — trocar um bit da entrada troca cada bit da saida com
 * probabilidade ~1/2. E o que VS16 cobra: um contador-based mal misturado
 * passa em "e deterministico" e falha em independencia, porque chaves vizinhas
 * (i, i+1) produzem saidas vizinhas.
 *
 * Restricao de §C: sem dependencias externas. Nada de Random123 como
 * biblioteca — mas a ideia dela (counter-based, stateless) e exatamente a
 * certa e cabe em cinco linhas.
 * ------------------------------------------------------------------------ */

static inline uint64_t
  mix64 (uint64_t x)
{
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}

/*
 * A chave empacotada em duas palavras de 64 bits.
 *
 *   w0 = a (32 bits) | b (32 bits)
 *   w1 = ano (32) | fase (8) | subpasso (24)
 *
 * Os campos cabem com folga larga: `a` e `b` sao coordenadas de grade ou
 * indices de iteracao, `fase` vai de 1 a 5, e `subpasso` usa [0,8] mais a
 * faixa da caminhada a partir de 16. Nao ha truncamento silencioso possivel
 * em nenhuma grade que este trabalho vá medir (S08 para em 2000x2000).
 *
 * Tres misturas: a primeira absorve a semente, as outras duas dobram as duas
 * palavras da chave. Custa cerca de 30 ciclos — contra alguns poucos do ran1.
 * E deliberado e esta orcado: §D1 mediu que sao 106 236 sorteios em 20 anos,
 * ou seja ~1 ms no total da corrida inteira. O custo por chamada e maior e o
 * custo agregado e irrelevante; T7 mede a diferenca em isolamento, que e o
 * numero honesto a apresentar antes das curvas de speedup (§N do spec).
 */
double
  rng_double (uint64_t semente, int ano, int fase, int a, int b, int sub)
{
  uint64_t w0;
  uint64_t w1;
  uint64_t x;

  w0 = ((uint64_t) (uint32_t) a << 32) | (uint64_t) (uint32_t) b;
  w1 = ((uint64_t) (uint32_t) ano << 32)
     | ((uint64_t) (uint32_t) fase << 24)
     | ((uint64_t) (uint32_t) sub & 0xffffffULL);

  x = mix64 (semente ^ 0x9e3779b97f4a7c15ULL);
  x = mix64 (x ^ w0);
  x = mix64 (x ^ w1);

  /*
   * 53 bits de mantissa em [0,1). O ran1 original satura em 1 - 1,2e-7 por
   * causa de um cuidado numerico do Numerical Recipes; aqui o limite superior
   * e estrutural — x >> 11 nunca alcanca 2^53 — entao nao ha o que saturar.
   */
  return (double) (x >> 11) * 0x1.0p-53;
}

/* ------------------------------------------------------------------------ *
 * Despacho
 * ------------------------------------------------------------------------ */

double
  rng_next (rng_ctx_t ctx, int sub)
{
  if (g_rng_mode == RNG_LEGACY)
    return RANNUM;

  return rng_double (g_rng_seed, proc_GetCurrentYear (),
                     ctx.fase, ctx.a, ctx.b, sub);
}

/*
 * A forma `(int) (x * n)` e a das macros RANDOM_INT/RANDOM_ROW/RANDOM_COL do
 * random.h original, preservada letra por letra. Em modo legacy isso importa
 * para V7; em modo anchored importa para que a comparacao legacy x anchored
 * de T7 isole a troca de gerador, e so ela.
 */
int
  rng_next_int (rng_ctx_t ctx, int sub, int n)
{
  return (int) (rng_next (ctx, sub) * n);
}

/* ------------------------------------------------------------------------ *
 * Inicializacao
 * ------------------------------------------------------------------------ */

void
  rng_init (int mode, long seed)
{
  g_rng_mode = mode;
  g_rng_seed = (uint64_t) labs (seed);

  /*
   * InitRandom semeia o ran1 e consome exatamente um sorteio — medido, nao
   * deduzido: a sonda harness/probe-rng.sh contabilizou 1 sorteio "fora das
   * fases" em 20 anos (§D1 do spec do estagio).
   *
   * Roda nos DOIS modos, de proposito. Em anchored o ran1 nunca e chamado, e
   * semea-lo e desperdicio de microssegundos; mas deixar o estado do ran1
   * identico nos dois modos significa que trocar --rng nao muda mais nada
   * alem do que se quer medir. Se um dia sobrar uma chamada a RANNUM em algum
   * caminho nao convertido, ela produzira o MESMO valor nos dois modos, e a
   * divergencia aparecera como diferenca de fluxo — nao como lixo semeado.
   */
  InitRandom (seed);
}

const char *
  rng_mode_name (void)
{
  return (g_rng_mode == RNG_LEGACY) ? "legacy" : "anchored";
}
