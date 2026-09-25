/*
 * grid.c — alocacao das matrizes globais.
 *
 * Ver grid.h para o papel de cada grade.
 */

#include <stdio.h>
#include <stdlib.h>

#include "grid.h"

int g_nrows;
int g_ncols;
int g_total_pixels;

GRID_P g_z;
GRID_P g_delta;
GRID_P g_slp;
GRID_P g_excld;
GRID_P g_roads;
GRID_P g_workspace;
GRID_P g_urban_seed;

/*
 * O workspace da fase 5 guarda duas listas de indices (linha e coluna) das
 * celulas que cresceram. No SLEUTH original as duas dividem um unico bloco,
 * e e ai que mora o bug B7 do SPEC.md:
 *
 *     growth_row = (int *) workspace;
 *     growth_col = (int *) workspace + (nrows);
 *
 * O TAMANHO do bloco esta correto: uma grade de total_pixels PIXELs (long,
 * 8 bytes em LP64) comporta exatamente 2 * total_pixels inteiros de 4 bytes.
 * O que esta errado e o DESLOCAMENTO: growth_col comeca apenas `nrows`
 * inteiros depois de growth_row, enquanto growth_row pode receber ate
 * `total_pixels` entradas. Numa grade 20x20 com crescimento moderado,
 * growth_row passa de 20 elementos e invade growth_col.
 *
 * Por isso a alocacao aqui e identica a do original (uma grade comum) e a
 * correcao mora em spread.c, spr_phase5, onde o deslocamento passa a ser
 * g_total_pixels.
 *
 * Que o bloco caiba esta garantido em dois pontos (§B8, tarefa T10): o
 * _Static_assert de globals.h, junto da definicao de PIXEL que pode invalidar
 * a conta, e um assert de `growth_count <= total_pixels` em spr_phase5.
 */
static GRID_P alloc_grid (const char *name)
{
  GRID_P p = calloc ((size_t) g_total_pixels, sizeof (PIXEL));
  if (p == NULL)
  {
    fprintf (stderr, "grid_alloc: sem memoria para a grade '%s' (%d pixels)\n",
             name, g_total_pixels);
    exit (EXIT_FAILURE);
  }
  return p;
}

void
  grid_alloc (int nrows, int ncols)
{
  if (nrows < 3 || ncols < 3)
  {
    /* as fases varrem o interior (1 .. n-2); abaixo de 3x3 nao sobra nada */
    fprintf (stderr, "grid_alloc: grade %dx%d e pequena demais (minimo 3x3)\n",
             nrows, ncols);
    exit (EXIT_FAILURE);
  }

  g_nrows = nrows;
  g_ncols = ncols;
  g_total_pixels = nrows * ncols;

  g_z         = alloc_grid ("z");
  g_delta     = alloc_grid ("delta");
  g_slp       = alloc_grid ("slp");
  g_excld     = alloc_grid ("excld");
  g_roads     = alloc_grid ("roads");
  g_workspace = alloc_grid ("workspace");
  g_urban_seed = alloc_grid ("urban_seed");
}

void
  grid_free (void)
{
  free (g_z);         g_z         = NULL;
  free (g_delta);     g_delta     = NULL;
  free (g_slp);       g_slp       = NULL;
  free (g_excld);     g_excld     = NULL;
  free (g_roads);     g_roads     = NULL;
  free (g_workspace); g_workspace = NULL;
  free (g_urban_seed); g_urban_seed = NULL;
}
