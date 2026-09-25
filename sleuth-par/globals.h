/*
 * globals.h — tipos, constantes e macros do nucleo do SLEUTH.
 *
 * Reune o que o spread.c precisa e que, na arvore original, estava espalhado
 * por tres cabecalhos:
 *
 *   SLEUTH/ugm_defines.h  tipos (PIXEL, GRID_P, COEFF_TYPE) e constantes
 *   SLEUTH/ugm_macros.h   OFFSET, IMAGE_PT, INTERIOR_PT, MAX, MIN
 *   SLEUTH/random.h       macros RANDOM_*
 *
 * O globals.h original nao serve: contem apenas variaveis globais de MPI.
 * Ver SPEC.md §B9.
 *
 * Diferenca deliberada em relacao ao original: OFFSET, IMAGE_PT e INTERIOR_PT
 * usavam chamadas a igrid_GetNumCols()/igrid_GetNumRows(). Aqui usam as
 * variaveis globais g_ncols/g_nrows. Ver SPEC.md §B3 e invariante V14.
 */
#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdbool.h>
#include <stdlib.h>
#include <math.h>

/* ------------------------------------------------------------------------ *
 * Tipos — de SLEUTH/ugm_defines.h, sem alteracao
 * ------------------------------------------------------------------------ */

#define PIXEL             long
#define GRID_P            PIXEL *
#define BOOLEAN           int
#define COEFF_TYPE        double
#define RANDOM_SEED_TYPE  long
#define BYTES_PER_PIXEL   sizeof (PIXEL)

#define TRUE   1
#define FALSE  0

/*
 * O workspace da fase 5 e uma grade comum — total_pixels PIXELs — reaproveitada
 * como DUAS listas de total_pixels `int` (linha e coluna das celulas em
 * crescimento). Ver spr_phase5 em spread.c e SPEC.md §B7.
 *
 * Que as duas listas caibam nao e projeto, e coincidencia desta plataforma: em
 * LP64 sizeof(PIXEL) e 8 e sizeof(int) e 4, entao cabe EXATAMENTE, sem folga.
 * Basta PIXEL virar `int`, ou `int` virar 64 bits, para a correcao do B7
 * passar a escrever fora da grade — e em silencio, porque o workspace e a
 * ultima grade alocada em grid.c.
 *
 * A asercao mora aqui, junto da definicao que ela protege, e nao no spread.c
 * que a consome: assim quem mexer em PIXEL ve a quebra no ato e em toda unidade
 * de compilacao, em vez de descobrir num despejo corrompido.
 * Ver §B8 de specs/S03-harness.md (tarefa T10).
 *
 * A mensagem e ASCII puro de proposito: o gcc imprime o literal byte a byte e
 * transforma UTF-8 em escapes octais, o que embaralharia justamente a linha
 * que alguem vai ler no momento da quebra.
 */
_Static_assert (sizeof (PIXEL) >= 2 * sizeof (int),
                "o workspace da fase 5 nao comporta duas listas de "
                "total_pixels ints - ver SPEC.md secao B7");

/* ------------------------------------------------------------------------ *
 * Constantes do modelo — de SLEUTH/ugm_defines.h, sem alteracao
 * ------------------------------------------------------------------------ */

/* tags de fase gravadas em delta, identificam qual regra urbanizou a celula */
#define PHASE0G  3
#define PHASE1G  4
#define PHASE2G  5
#define PHASE3G  6
#define PHASE4G  7
#define PHASE5G  8

#define MIN_NGHBR_TO_SPREAD  2
#define MAX_ROAD_VALUE       100

/* usada por spr_get_slp_weights para normalizar o coeficiente de declividade */
#define MIN_SLOPE_RESISTANCE_VALUE   0.01
#define MAX_SLOPE_RESISTANCE_VALUE   100.0

/* operadores de comparacao aceitos por util_condition_gif / util_count_* */
#define LT  0
#define LE  1
#define EQ  2
#define NE  3
#define GE  4
#define GT  5

/* ------------------------------------------------------------------------ *
 * Dimensoes da grade — globais (SPEC.md §C, orientacao do professor)
 *
 * Definidas em grid.c. Imutaveis depois de grid_alloc(): e essa imutabilidade
 * que permite as threads lerem OFFSET sem sincronizacao (V2).
 * ------------------------------------------------------------------------ */

extern int g_nrows;
extern int g_ncols;
extern int g_total_pixels;

/* ------------------------------------------------------------------------ *
 * Macros de indexacao — de SLEUTH/ugm_macros.h, com g_* no lugar das chamadas
 * ------------------------------------------------------------------------ */

#define MAX(a,b)  (((a) > (b)) ? (a) : (b))
#define MIN(a,b)  (((a) < (b)) ? (a) : (b))

#define OFFSET(i,j)  ((i) * g_ncols + (j))

#define IMAGE_PT(row,col)     \
        (((row) <  g_nrows) &&  \
         ((col) <  g_ncols) &&  \
         ((row) >= 0)       &&  \
         ((col) >= 0))

#define INTERIOR_PT(row,col)      \
        (((row) < g_nrows - 1) &&   \
         ((col) < g_ncols - 1) &&   \
         ((row) > 0)           &&   \
         ((col) > 0))

#include "random.h"
#include "stubs.h"

/*
 * rng.h vem DEPOIS dos dois acima porque depende deles: o despacho do modo
 * legacy chama RANNUM (random.h) e a chave le proc_GetCurrentYear (stubs.h).
 * A partir do estagio S04 e o rng.h que os pontos de sorteio consomem; as
 * macros RANDOM_* de random.h continuam existindo, mas so o modo legacy as
 * alcanca, por dentro de rng_next.
 */
#include "rng.h"

#endif /* GLOBALS_H */
