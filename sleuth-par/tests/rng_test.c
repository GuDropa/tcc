/*
 * rng_test.c — VS15 e VS16 do estagio S04.
 *
 * VS15  rng_double e pura: a mesma chave da o mesmo valor, em qualquer ordem
 *       de chamada e apos qualquer numero de chamadas anteriores.
 *
 * VS16  chaves vizinhas nao se correlacionam, e a media fica em 0,5 +- 0,01
 *       em 10^6 amostras.
 *
 * Por que VS16 existe separada de VS15: um gerador contador-based MAL
 * MISTURADO passa em VS15 com folga — ele e deterministico por construcao, e
 * "deterministico" e exatamente o que VS15 mede. O que ele falha e em produzir
 * valores INDEPENDENTES para chaves vizinhas, e chaves vizinhas sao o caso
 * comum aqui: a fase 4 varre (row, col) em sequencia, entao (i,j) e (i,j+1)
 * sao consultadas uma atras da outra o tempo todo. Um misturador fraco
 * devolveria para elas valores proximos, e o modelo ganharia correlacao
 * espacial que o ran1 nao tinha — viés silencioso, invisivel em qualquer teste
 * de reprodutibilidade.
 *
 * Compila com: make tests/rng_test   (ver Makefile)
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "globals.h"
#include "rng.h"

#define SEMENTE  1ULL

static int falhas = 0;

static void
  ok (int cond, const char *rotulo, const char *detalhe)
{
  if (cond)
  {
    printf ("  ok    %s%s%s\n", rotulo,
            detalhe[0] ? " — " : "", detalhe);
  }
  else
  {
    printf ("  FALHA %s%s%s\n", rotulo,
            detalhe[0] ? " — " : "", detalhe);
    falhas++;
  }
}

/* ------------------------------------------------------------------------ *
 * VS15 — pureza
 * ------------------------------------------------------------------------ */

typedef struct
{
  int ano, fase, a, b, sub;
  double valor;
} amostra_t;

static void
  vs15 (void)
{
  enum { N = 10000 };
  static amostra_t v[N];
  int i;
  int ordem_ok = 1;
  int repeticao_ok = 1;

  printf ("\nVS15 rng_double e pura: mesma chave, mesmo valor\n");

  /* 1a passada: ordem direta, gravando o que saiu */
  for (i = 0; i < N; i++)
  {
    v[i].ano  = 1990 + (i % 20);
    v[i].fase = 1 + (i % 5);
    v[i].a    = i % 197;
    v[i].b    = (i * 31) % 197;
    v[i].sub  = i % 9;
    v[i].valor = rng_double (SEMENTE, v[i].ano, v[i].fase,
                             v[i].a, v[i].b, v[i].sub);
  }

  /*
   * 2a passada: ordem EMBARALHADA. Um gerador com estado escondido entregaria
   * valores diferentes aqui, porque o que ele devolve depende de quantas
   * chamadas vieram antes. E precisamente o defeito do ran1 que este estagio
   * remove, e a razao de a fase 4 nao poder ser repartida entre threads hoje.
   */
  {
    unsigned long long r = 88172645463325252ULL;
    for (i = N - 1; i > 0; i--)
    {
      int j;
      amostra_t t;
      r ^= r << 13; r ^= r >> 7; r ^= r << 17;   /* xorshift, so para embaralhar */
      j = (int) (r % (unsigned long long) (i + 1));
      t = v[i]; v[i] = v[j]; v[j] = t;
    }
  }

  for (i = 0; i < N; i++)
  {
    double agora = rng_double (SEMENTE, v[i].ano, v[i].fase,
                               v[i].a, v[i].b, v[i].sub);
    if (agora != v[i].valor)
    {
      ordem_ok = 0;
      break;
    }
  }
  ok (ordem_ok, "10^4 chaves em ordem embaralhada", "bit a bit iguais");

  /* a mesma chave mil vezes seguidas */
  {
    double primeiro = rng_double (SEMENTE, 1995, RNG_FASE_4, 42, 17,
                                  RNG_SUB_SPREAD);
    for (i = 0; i < 1000; i++)
      if (rng_double (SEMENTE, 1995, RNG_FASE_4, 42, 17,
                      RNG_SUB_SPREAD) != primeiro)
      {
        repeticao_ok = 0;
        break;
      }
    ok (repeticao_ok, "mesma chave 1000 vezes", "sem deriva");
  }

  /* sementes diferentes tem de divergir */
  ok (rng_double (1, 1995, RNG_FASE_4, 10, 10, RNG_SUB_SPREAD) !=
      rng_double (2, 1995, RNG_FASE_4, 10, 10, RNG_SUB_SPREAD),
      "semente 1 e semente 2 divergem", "");

  /* e todo campo da chave tem de ser observavel na saida */
  {
    double base = rng_double (SEMENTE, 1995, RNG_FASE_4, 10, 10, RNG_SUB_SPREAD);
    int todos = 1;
    todos &= (rng_double (SEMENTE, 1996, RNG_FASE_4, 10, 10, RNG_SUB_SPREAD) != base);
    todos &= (rng_double (SEMENTE, 1995, RNG_FASE_5, 10, 10, RNG_SUB_SPREAD) != base);
    todos &= (rng_double (SEMENTE, 1995, RNG_FASE_4, 11, 10, RNG_SUB_SPREAD) != base);
    todos &= (rng_double (SEMENTE, 1995, RNG_FASE_4, 10, 11, RNG_SUB_SPREAD) != base);
    todos &= (rng_double (SEMENTE, 1995, RNG_FASE_4, 10, 10, RNG_SUB_EXCLD)  != base);
    ok (todos, "ano, fase, a, b e subpasso mudam a saida",
        "nenhum campo e ignorado");
  }
}

/* ------------------------------------------------------------------------ *
 * VS16 — distribuicao e independencia
 * ------------------------------------------------------------------------ */

/*
 * Varre 10^6 chaves mexendo em UM unico campo e cobra quatro coisas:
 *
 *   media       0,5 +- 0,01        (o que o spec pede explicitamente)
 *   variancia   1/12 +- 0,002      (uniforme, nao so centrada — uma saida que
 *                                   alternasse 0 e 1 teria media 0,5 perfeita)
 *   autocorr.   |r| < 0,01 no lag 1 — chaves VIZINHAS, que e o caso da fase 4
 *   baldes      qui-quadrado sobre 10 baldes, 9 g.l., p = 0,001
 *
 * SOBRE O QUI-QUADRADO. A primeira versao deste teste cobrava cada balde
 * dentro de +-1 % do esperado, e a varredura do ano reprovou. Nao era o
 * gerador: com 10^6 amostras o desvio padrao de um balde e sqrt(10^6 * 0,1 *
 * 0,9) = 300, entao 1 % de 100 000 sao 3,33 sigma. Exigir isso de dez baldes
 * em quatro varreduras reprova por acaso com probabilidade ~3 %. Media,
 * variancia, autocorrelacao e avalanche passavam todas com folga na mesma
 * rodada — o que reprovava era o criterio.
 *
 * Qui-quadrado e o instrumento certo porque agrega os dez desvios num numero
 * so, com distribuicao conhecida: 9 graus de liberdade, valor critico 27,88
 * para p = 0,001. Fica registrado aqui porque e o tipo de falsa falha que ja
 * custou tempo a este projeto duas vezes (§B14 do mestre) e o padrao se
 * repete: o codigo estava certo e o criterio de comparacao, errado.
 */
#define QUIQUAD_CRITICO_9GL  27.88
typedef enum { EIXO_SUB, EIXO_A, EIXO_B, EIXO_ANO } eixo_t;

static void
  varredura (eixo_t eixo, const char *nome)
{
  const long N = 1000000L;
  long i;
  double soma = 0.0, soma_q = 0.0;
  double soma_xy = 0.0;
  double anterior = 0.0;
  long baldes[10] = {0};
  double media, variancia, r, quiquad = 0.0;
  char detalhe[160];

  for (i = 0; i < N; i++)
  {
    double x;
    switch (eixo)
    {
    case EIXO_SUB: x = rng_double (SEMENTE, 1995, RNG_FASE_4, 10, 10, (int) i); break;
    case EIXO_A:   x = rng_double (SEMENTE, 1995, RNG_FASE_4, (int) i, 10, RNG_SUB_SPREAD); break;
    case EIXO_B:   x = rng_double (SEMENTE, 1995, RNG_FASE_4, 10, (int) i, RNG_SUB_SPREAD); break;
    default:       x = rng_double (SEMENTE, (int) i, RNG_FASE_4, 10, 10, RNG_SUB_SPREAD); break;
    }

    soma   += x;
    soma_q += x * x;
    if (i > 0)
      soma_xy += anterior * x;
    anterior = x;
    baldes[(int) (x * 10.0)]++;
  }

  media     = soma / (double) N;
  variancia = soma_q / (double) N - media * media;

  /* Pearson no lag 1, com media e variancia da propria amostra */
  r = (soma_xy / (double) (N - 1) - media * media) / variancia;

  for (i = 0; i < 10; i++)
  {
    double esperado = (double) N / 10.0;
    double d = (double) baldes[i] - esperado;
    quiquad += d * d / esperado;
  }

  snprintf (detalhe, sizeof detalhe,
            "media %.5f · var %.5f · r(lag1) %+.5f · qui2 %.2f (crit %.2f)",
            media, variancia, r, quiquad, QUIQUAD_CRITICO_9GL);

  ok (fabs (media - 0.5) < 0.01 &&
      fabs (variancia - 1.0 / 12.0) < 0.002 &&
      fabs (r) < 0.01 &&
      quiquad < QUIQUAD_CRITICO_9GL,
      nome, detalhe);
}

/*
 * Avalanche: trocar UM bit da chave tem de trocar cerca de metade dos bits da
 * saida. E o teste que separa "misturador de verdade" de "misturador que so
 * parece": um gerador que somasse os campos e multiplicasse por uma constante
 * passaria em media e variancia e falharia aqui, porque chaves vizinhas
 * ficariam a poucos bits de distancia.
 */
static void
  avalanche (void)
{
  const long N = 20000L;
  long i;
  long total_bits = 0;
  double fracao;
  char detalhe[96];

  for (i = 0; i < N; i++)
  {
    double x = rng_double (SEMENTE, 1995, RNG_FASE_4, (int) i, 10, RNG_SUB_SPREAD);
    double y = rng_double (SEMENTE, 1995, RNG_FASE_4, (int) i + 1, 10, RNG_SUB_SPREAD);
    unsigned long long bx = (unsigned long long) (x * 9007199254740992.0);
    unsigned long long by = (unsigned long long) (y * 9007199254740992.0);
    unsigned long long d = bx ^ by;
    while (d) { total_bits += (long) (d & 1ULL); d >>= 1; }
  }

  fracao = (double) total_bits / ((double) N * 53.0);
  snprintf (detalhe, sizeof detalhe,
            "%.4f dos 53 bits mudam entre a=i e a=i+1 (esperado ~0,5)", fracao);
  ok (fabs (fracao - 0.5) < 0.02, "avalanche em chaves adjacentes", detalhe);
}

static void
  vs16 (void)
{
  printf ("\nVS16 chaves vizinhas nao se correlacionam\n");
  varredura (EIXO_SUB, "variando so o subpasso");
  varredura (EIXO_A,   "variando so a (linha na fase 4)");
  varredura (EIXO_B,   "variando so b (coluna na fase 4)");
  varredura (EIXO_ANO, "variando so o ano");
  avalanche ();
}

int
  main (void)
{
  printf ("== S04: verificacao do gerador ancorado ==\n");

  vs15 ();
  vs16 ();

  if (falhas == 0)
  {
    printf ("\n== VS15 e VS16 passaram ==\n");
    return EXIT_SUCCESS;
  }
  printf ("\n== %d verificacao(oes) FALHARAM ==\n", falhas);
  return EXIT_FAILURE;
}
