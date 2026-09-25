/*
 * load.c — leitura das grades de entrada em texto. Ver load.h.
 *
 * Duas decisoes de implementacao que valem comentario:
 *
 * 1. A leitura e por fscanf ("%ld") em vez de linha a linha. Nao e elegancia:
 *    e o que permite ler uma grade de 2000x2000 sem nenhum buffer de linha, e
 *    portanto sem nenhuma alocacao fora de grid_alloc (invariante V9).
 *
 * 2. Os quatro arquivos sao abertos ANTES de grid_alloc, porque as dimensoes
 *    saem do cabecalho e tem de ser conferidas entre si antes de alocar
 *    qualquer coisa. Se um deles discordar, o programa morre sem ter alocado
 *    nada.
 *
 * O carregador nao transforma nada. A tentacao de aplicar aqui o PHASE0G ou a
 * normalizacao de estradas existe, mas isso quebraria a separacao que o
 * harness estabeleceu (specs/S03-harness.md §D3): o leitor le, o mkdata.sh
 * transforma, e por isso um erro em cada um tem sintoma diferente.
 */

#include <stdio.h>
#include <stdlib.h>

#include "globals.h"
#include "grid.h"
#include "load.h"

#define LOAD_PATH_MAX  1024
#define LOAD_NGRIDS    4

static const char *const load_names[LOAD_NGRIDS] =
{
  "z.txt", "roads.txt", "excld.txt", "slp.txt"
};

/*
 * Abre DIR/NAME e consome o cabecalho "nrows ncols".
 * Devolve o FILE* posicionado no primeiro valor do corpo.
 */
static FILE *
  open_with_header (const char *dir, const char *name, int *nrows, int *ncols)
{
  char path[LOAD_PATH_MAX];
  FILE *fp;
  int written;

  written = snprintf (path, sizeof path, "%s/%s", dir, name);
  if (written < 0 || (size_t) written >= sizeof path)
  {
    fprintf (stderr, "load: caminho longo demais: %s/%s\n", dir, name);
    exit (EXIT_FAILURE);
  }

  fp = fopen (path, "r");
  if (fp == NULL)
  {
    fprintf (stderr, "load: nao consegui abrir '%s'\n", path);
    exit (EXIT_FAILURE);
  }

  if (fscanf (fp, "%d %d", nrows, ncols) != 2)
  {
    fprintf (stderr,
             "load: '%s' nao comeca com o cabecalho 'nrows ncols'\n", path);
    exit (EXIT_FAILURE);
  }

  if (*nrows < 3 || *ncols < 3)
  {
    fprintf (stderr, "load: '%s' declara grade %dx%d (minimo 3x3)\n",
             path, *nrows, *ncols);
    exit (EXIT_FAILURE);
  }

  return fp;
}

/*
 * Le exatamente g_total_pixels valores para dst, e confere que o arquivo
 * acaba ai. Sobra de valores e tao suspeita quanto falta: significa que o
 * cabecalho nao descreve o corpo.
 */
static void
  read_body (FILE *fp, GRID_P dst, const char *name)
{
  int i;
  long v;

  for (i = 0; i < g_total_pixels; i++)
  {
    if (fscanf (fp, "%ld", &v) != 1)
    {
      fprintf (stderr,
               "load: '%s' acabou no valor %d de %d (cabecalho diz %dx%d)\n",
               name, i, g_total_pixels, g_nrows, g_ncols);
      exit (EXIT_FAILURE);
    }
    dst[i] = (PIXEL) v;
  }

  if (fscanf (fp, "%ld", &v) == 1)
  {
    fprintf (stderr,
             "load: '%s' tem mais valores do que os %dx%d do cabecalho\n",
             name, g_nrows, g_ncols);
    exit (EXIT_FAILURE);
  }
}

void
  load_input_dir (const char *dir)
{
  FILE  *fp[LOAD_NGRIDS];
  int    rows[LOAD_NGRIDS];
  int    cols[LOAD_NGRIDS];
  GRID_P target[LOAD_NGRIDS];
  int    k;

  for (k = 0; k < LOAD_NGRIDS; k++)
    fp[k] = open_with_header (dir, load_names[k], &rows[k], &cols[k]);

  for (k = 1; k < LOAD_NGRIDS; k++)
  {
    if (rows[k] != rows[0] || cols[k] != cols[0])
    {
      fprintf (stderr,
               "load: cabecalhos discordam — %s e %dx%d, %s e %dx%d\n",
               load_names[0], rows[0], cols[0],
               load_names[k], rows[k], cols[k]);
      exit (EXIT_FAILURE);
    }
  }

  grid_alloc (rows[0], cols[0]);

  target[0] = g_z;
  target[1] = g_roads;
  target[2] = g_excld;
  target[3] = g_slp;

  for (k = 0; k < LOAD_NGRIDS; k++)
  {
    read_body (fp[k], target[k], load_names[k]);
    fclose (fp[k]);
  }
}
