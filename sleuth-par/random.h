/*
 * random.h — gerador de numeros aleatorios.
 *
 * Adaptado de SLEUTH/random.h. O algoritmo em random.c e identico ao original
 * (ran1 do Numerical Recipes) — trocar o gerador e assunto do estagio S04, nao
 * deste. A unica mudanca aqui e RANDOM_ROW/RANDOM_COL usarem g_nrows/g_ncols
 * em vez de igrid_GetNumRows()/igrid_GetNumCols(). Os valores produzidos sao
 * os mesmos; ver SPEC.md §B3.
 *
 * ATENCAO (SPEC.md §B5): ran_random guarda estado em variaveis static dentro
 * da propria funcao (iv[32], iy), alem do ran_seed global. E nao reentrante
 * por construcao. Nao chamar de mais de uma thread antes de S04.
 */
#ifndef RANDOM_H
#define RANDOM_H

extern RANDOM_SEED_TYPE ran_seed;

#define RANNUM  ran_random (&ran_seed)

#define RANDOM_ROW     ((int) (RANNUM * g_nrows))
#define RANDOM_COL     ((int) (RANNUM * g_ncols))
#define RANDOM_INT(a)  ((int) (RANNUM * (a)))
#define RANDOM_FLOAT   (RANNUM)

double ran_random (RANDOM_SEED_TYPE *ran_idum);
void   InitRandom (RANDOM_SEED_TYPE seed);

#endif /* RANDOM_H */
