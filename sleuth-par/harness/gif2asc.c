/*
 * gif2asc.c — converte um GIF em tons de cinza para a grade em texto.
 *
 * FERRAMENTA DE HARNESS, NAO PARTE DO PROTOTIPO.
 *
 * A restricao §C do spec mestre tira todo o subsistema gdif_obj/GD do
 * prototipo. Ela continua valendo: nenhum objeto do GD e linkado ao binario
 * sleuth-par. Este programa e um passo de preparacao de dados, roda uma vez,
 * e o que ele produz (texto) e o que vai versionado. O nucleo le texto.
 *
 * O laco de conversao e uma replica literal de SLEUTH/gdif_obj.c:359-380:
 *
 *     index_val = (unsigned short int) gdImageGetPixel (im_in, j, i);
 *     red   = gdImageRed   (im_in, index_val);
 *     green = gdImageGreen (im_in, index_val);
 *     blue  = gdImageBlue  (im_in, index_val);
 *     if ((red == green) && (red == blue))
 *       gif_ptr[OFFSET (i, j)] = red;
 *     else
 *       ERRO: "file:%s is not a true gray scale image"
 *
 * Ou seja: o valor da grade e o componente vermelho da cor indexada, e a
 * imagem tem de ser cinza de verdade. Nenhuma transformacao e aplicada aqui
 * — a normalizacao de estradas de igrid_NormalizeRoads e assunto do
 * mkdata.sh, para que este arquivo continue sendo so o leitor.
 *
 * Formato de saida (o mesmo que o --dump do sleuth-par produz, para que dump
 * e entrada sejam intercambiaveis):
 *
 *     <nrows> <ncols>
 *     v v v ... v      <- ncols valores, nrows linhas
 *
 * Uso:
 *     gif2asc ARQUIVO.gif            grade em texto no stdout
 *     gif2asc ARQUIVO.gif --stats    histograma, uma linha (para a VS8)
 *
 * Compilar: gcc -std=gnu89 -w   (o gd.c e de 1994 e nao passa em -Wall)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gd.h"

int
main (int argc, char **argv)
{
  FILE *in;
  gdImagePtr im;
  int i, j, idx, red, green, blue;
  int stats_only = 0;
  long hist[256];

  if (argc < 2 || argc > 3)
  {
    fprintf (stderr, "uso: %s ARQUIVO.gif [--stats]\n", argv[0]);
    return 2;
  }
  if (argc == 3)
  {
    if (strcmp (argv[2], "--stats") != 0)
    {
      fprintf (stderr, "%s: opcao desconhecida '%s'\n", argv[0], argv[2]);
      return 2;
    }
    stats_only = 1;
  }

  if ((in = fopen (argv[1], "rb")) == NULL)
  {
    perror (argv[1]);
    return 1;
  }
  im = gdImageCreateFromGif (in);
  fclose (in);
  if (im == NULL)
  {
    fprintf (stderr, "%s: o GD nao conseguiu decodificar o GIF\n", argv[1]);
    return 1;
  }

  for (i = 0; i < 256; i++)
    hist[i] = 0;

  /*
   * O original varre colunas no laco externo; aqui e linhas, porque a saida
   * e por linha. A ordem de varredura nao afeta o resultado: gdImageGetPixel
   * e uma consulta pura.
   */
  if (!stats_only)
    printf ("%d %d\n", im->sy, im->sx);

  for (i = 0; i < im->sy; i++)
  {
    for (j = 0; j < im->sx; j++)
    {
      idx   = gdImageGetPixel (im, j, i);
      red   = gdImageRed   (im, idx);
      green = gdImageGreen (im, idx);
      blue  = gdImageBlue  (im, idx);

      if (red != green || red != blue)
      {
        fprintf (stderr,
                 "%s: nao e imagem em tons de cinza de verdade\n"
                 "  em (linha %d, coluna %d): indice=%d RGB=(%d,%d,%d)\n",
                 argv[1], i, j, idx, red, green, blue);
        gdImageDestroy (im);
        return 1;
      }

      hist[red & 0xff]++;
      if (!stats_only)
        printf ("%d%c", red, (j == im->sx - 1) ? '\n' : ' ');
    }
  }

  if (stats_only)
  {
    long nonzero = 0;
    int max = 0;
    int distinct = 0;

    for (i = 1; i < 256; i++)
      if (hist[i])
      {
        nonzero += hist[i];
        max = i;
      }
    for (i = 0; i < 256; i++)
      if (hist[i])
        distinct++;

    printf ("%-32s %4dx%-4d nao-zero=%6ld max=%3d distintos=%3d  {",
            argv[1], im->sy, im->sx, nonzero, max, distinct);
    for (i = 0; i < 256; i++)
      if (hist[i])
        printf ("%d:%ld ", i, hist[i]);
    printf ("}\n");
  }

  gdImageDestroy (im);
  return 0;
}
