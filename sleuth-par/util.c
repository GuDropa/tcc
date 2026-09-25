/*
 * util.c — as cinco funcoes de SLEUTH/utilities.c que o nucleo consome.
 *
 * Portadas sem alteracao semantica. O que saiu: FUNC_INIT/FUNC_END (pilha de
 * chamadas por strcpy em array global) e LOG_ERROR (que escreve em arquivo de
 * log do cenario). Os asserts do original foram mantidos.
 *
 * O original tem 17 funcoes util_*; as outras 12 servem a leitura de GIF,
 * estatisticas de calibracao e composicao de imagens de saida — tudo fora do
 * escopo (SPEC.md §C).
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "globals.h"
#include "grid.h"

static void
  bad_option (const char *who, int option)
{
  fprintf (stderr, "%s: opcao de comparacao desconhecida = %d\n", who, option);
  exit (EXIT_FAILURE);
}

/* ------------------------------------------------------------------------ *
 * AS DUAS VARREDURAS MAIS SIMPLES DO TRABALHO — estagio S05, tarefa T3.
 *
 * Ganharam uma versao "_range", que opera em [lo, hi) em vez de na grade
 * inteira. Nao ha mais nada: a versao original virou uma chamada com
 * [0, total_pixels), e os corpos sao os mesmos.
 *
 * E deliberado que seja tao pouco. Estas duas sao o CASO-BASE do estagio —
 * decidem cada pixel olhando so para aquele pixel, sem vizinhanca, sem
 * acumulador e sem ordem —, e o que o texto (§3.3) precisa mostrar e
 * justamente que o particionamento perfeito nao exige mecanismo nenhum: basta
 * recortar o intervalo. As outras duas varreduras do estagio precisam de
 * reducao (o merge) e de exclusao mutua (a fase 4), e e contra estas aqui que
 * elas se comparam.
 *
 * SOBRE lo == hi. Faixa vazia e legitima e acontece de verdade: com
 * --threads maior que o numero de linhas, os workers do fim ficam sem nada.
 * Por isso a asercao aqui e `lo <= hi`, e nao a `num_pixels > 0` do original
 * — que continua valendo para quem chama a forma de grade inteira.
 * ------------------------------------------------------------------------ */

void
  util_init_grid_range (GRID_P gif, PIXEL value, int lo, int hi)
{
  int i;

  assert (gif != NULL);
  assert (lo >= 0);
  assert (lo <= hi);

  for (i = lo; i < hi; i++)
  {
    gif[i] = value;
  }
}

void
  util_init_grid (GRID_P gif, PIXEL value)
{
  int total_pixels;

  total_pixels = mem_GetTotalPixels ();

  assert (gif != NULL);
  assert (total_pixels > 0);

  util_init_grid_range (gif, value, 0, total_pixels);
}

void
  util_condition_gif_range (int lo, int hi, GRID_P source, int option,
                            int cmp_value, GRID_P target, int set_value)
{
  int i;

  assert (source != NULL);
  assert (target != NULL);
  assert (lo >= 0);
  assert (lo <= hi);

#define COND_LOOP(OP)                                     \
  for (i = lo; i < hi; i++)                               \
    if (source[i] OP cmp_value) target[i] = set_value

  switch (option)
  {
  case LT: COND_LOOP (<);  break;
  case LE: COND_LOOP (<=); break;
  case EQ: COND_LOOP (==); break;
  case NE: COND_LOOP (!=); break;
  case GE: COND_LOOP (>=); break;
  case GT: COND_LOOP (>);  break;
  default:
    bad_option ("util_condition_gif", option);
  }

#undef COND_LOOP
}

void
  util_condition_gif (int num_pixels, GRID_P source, int option,
                      int cmp_value, GRID_P target, int set_value)
{
  assert (num_pixels > 0);

  util_condition_gif_range (0, num_pixels, source, option, cmp_value,
                            target, set_value);
}

int
  util_count_pixels (int num_pixels, GRID_P pixels, int option, int value)
{
  int i;
  int count = 0;

  assert (num_pixels > 0);
  assert (pixels != NULL);

  switch (option)
  {
  case LT:
    for (i = 0; i < num_pixels; i++) if (pixels[i] <  value) count++;
    break;
  case LE:
    for (i = 0; i < num_pixels; i++) if (pixels[i] <= value) count++;
    break;
  case EQ:
    for (i = 0; i < num_pixels; i++) if (pixels[i] == value) count++;
    break;
  case NE:
    for (i = 0; i < num_pixels; i++) if (pixels[i] != value) count++;
    break;
  case GE:
    for (i = 0; i < num_pixels; i++) if (pixels[i] >= value) count++;
    break;
  case GT:
    for (i = 0; i < num_pixels; i++) if (pixels[i] >  value) count++;
    break;
  default:
    bad_option ("util_count_pixels", option);
  }
  return count;
}

/*
 * Conta quantos dos 8 vizinhos satisfazem a comparacao.
 *
 * Nao ha verificacao de borda: o chamador e responsavel por passar um ponto
 * interior. spr_phase4 varre de 1 a n-2 justamente por isso. Mantido como no
 * original — e um ponto a vigiar quando as faixas de linhas forem divididas
 * entre threads (S05), porque a primeira e a ultima linha de cada faixa leem
 * a faixa vizinha.
 */
int
  util_count_neighbors (GRID_P grid, int i, int j, int option, PIXEL value)
{
  int count = 0;

  assert (grid != NULL);

#define NGHBR_SUM(OP)                                    \
  ( ((grid[OFFSET (i - 1, j - 1)] OP value) ? 1 : 0)   + \
    ((grid[OFFSET (i - 1, j    )] OP value) ? 1 : 0)   + \
    ((grid[OFFSET (i - 1, j + 1)] OP value) ? 1 : 0)   + \
    ((grid[OFFSET (i    , j - 1)] OP value) ? 1 : 0)   + \
    ((grid[OFFSET (i    , j + 1)] OP value) ? 1 : 0)   + \
    ((grid[OFFSET (i + 1, j - 1)] OP value) ? 1 : 0)   + \
    ((grid[OFFSET (i + 1, j    )] OP value) ? 1 : 0)   + \
    ((grid[OFFSET (i + 1, j + 1)] OP value) ? 1 : 0) )

  switch (option)
  {
  case LT: count = NGHBR_SUM (<);  break;
  case LE: count = NGHBR_SUM (<=); break;
  case EQ: count = NGHBR_SUM (==); break;
  case NE: count = NGHBR_SUM (!=); break;
  case GE: count = NGHBR_SUM (>=); break;
  case GT: count = NGHBR_SUM (>);  break;
  default:
    bad_option ("util_count_neighbors", option);
  }

#undef NGHBR_SUM

  return count;
}

/*
 * Vizinhos em sentido horario a partir do canto superior esquerdo:
 *
 *      0 | 7 | 6
 *      1 | * | 5
 *      2 | 3 | 4
 *
 * index de 0 a 7 devolve aquele vizinho; index == -1 devolve o PROXIMO da
 * sequencia, continuando de onde a ultima chamada parou.
 *
 * REENTRANTE A PARTIR DO ESTAGIO S04 (tarefa T5, §D5 do spec).
 *
 * O "de onde a ultima chamada parou" morava em `static int last_index` —
 * estado global mutavel, exatamente como o iv[]/iy de ran_random (§B5 do
 * mestre). Duas threads chamando esta funcao embaralhavam a sequencia de
 * vizinhos uma da outra, e isso NAO estava coberto pela regiao critica
 * identificada no plano (o read-modify-write sobre delta em spr_urbanize):
 * aqui nao ha escrita em grade nenhuma, e ainda assim havia corrida. Era o
 * segundo dos tres estados escondidos catalogados em §B12 do mestre.
 *
 * O conserto e o mais barato dos tres: o cursor vira propriedade do chamador,
 * um `int *` que vive na pilha dele. Nao ha trava, nao ha estado por thread,
 * e a sequencia de vizinhos nao muda.
 *
 * POR QUE A SEQUENCIA NAO MUDA, e nao e so esperanca. O cursor global so era
 * INCREMENTADO na chamada com index == -1, e nos dois unicos consumidores —
 * spr_get_neighbor e spr_road_walk — a primeira chamada de cada rodada sempre
 * passa um indice explicito de 0 a 7, que ATRIBUI o cursor. Ele portanto
 * nunca carregava informacao de uma rodada para a proxima: era global sem
 * precisar ser. Tornar o cursor local nao pode alterar o modo legacy, e VS18
 * confere isso contra o dump pre-S04.
 */
void
  util_get_next_neighbor (int i_in, int j_in, int *i_out, int *j_out,
                          int index, int *cursor)
{
  int i_adj;
  int j_adj;
  int row[8] = {-1,  0,  1, 1, 1, 0, -1, -1};
  int col[8] = {-1, -1, -1, 0, 1, 1,  1,  0};

  assert (g_nrows > i_in);
  assert (g_ncols > j_in);
  assert (i_in >= 0);
  assert (j_in >= 0);
  assert (i_out != NULL);
  assert (j_out != NULL);
  assert (cursor != NULL);
  assert (index >= -1);
  assert (index <= 7);

  if (index == -1)
  {
    (*cursor)++;
    (*cursor) = (*cursor) % 8;
  }
  else
  {
    (*cursor) = index;
  }
  i_adj = row[*cursor];
  j_adj = col[*cursor];
  (*i_out) = i_in + i_adj;
  (*j_out) = j_in + j_adj;
}
