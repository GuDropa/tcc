/*
 * random.c — gerador de numeros aleatorios do SLEUTH.
 *
 * Portado de SLEUTH/random.c. O algoritmo e o ran1 do Numerical Recipes in C
 * (Park-Miller com embaralhamento de Bays-Durham) e esta reproduzido aqui
 * SEM NENHUMA ALTERACAO NUMERICA — as constantes 16807, 127773, 2836 e
 * 2147483647, a tabela iv[32] e a ordem das operacoes sao as mesmas. Isso e
 * o que torna a invariante V7 (serial == SLEUTH original) verificavel.
 *
 * O que saiu: FUNC_INIT/FUNC_END (rastreamento de pilha de chamadas por
 * strcpy em array global) e os includes de igrid_obj.h / landclass_obj.h,
 * que nada tinham a ver com o gerador.
 *
 * PROBLEMA CONHECIDO (SPEC.md §B5): iv[] e iy sao `static` dentro da funcao.
 * O gerador carrega estado mutavel escondido alem do ran_seed global, e por
 * isso e NAO REENTRANTE: duas threads chamando ran_random corrompem a tabela
 * de embaralhamento, nao apenas a sequencia. Substituir por um gerador
 * contador-based ancorado a celula e o objeto do estagio S04.
 *
 * (C) Copr. 1986-92 Numerical Recipes Software.
 */

#include "globals.h"

RANDOM_SEED_TYPE ran_seed;

double
  ran_random (RANDOM_SEED_TYPE *ran_idum)
{
  int j;
  int k;
  static RANDOM_SEED_TYPE iv[32];
  static RANDOM_SEED_TYPE iy;
  double temp;
  double random_num;

  if ((*ran_idum) <= 0 || !iy)
  {
    if (-(*ran_idum) < 1)
    {
      (*ran_idum) = 1;
    }
    else
    {
      (*ran_idum) = -(*ran_idum);
    }
    for (j = 32 + 7; j >= 0; j--)
    {
      k = (*ran_idum) / 127773;
      (*ran_idum) = 16807 * ((*ran_idum) - k * 127773) - 2836 * k;
      if ((*ran_idum) < 0)
      {
        (*ran_idum) += 2147483647;
      }
      if (j < 32)
      {
        iv[j] = (*ran_idum);
      }
    }
    iy = iv[0];
  }
  k = (*ran_idum) / 127773;
  (*ran_idum) = 16807 * ((*ran_idum) - k * 127773) - 2836 * k;
  if ((*ran_idum) < 0)
  {
    (*ran_idum) += 2147483647;
  }
  j = iy / (1 + (2147483647 - 1) / 32);
  iy = iv[j];
  iv[j] = (*ran_idum);
  if ((temp = (1.0 / 2147483647) * iy) > (1.0 - 1.2e-7))
  {
    random_num = 1.0 - 1.2e-7;
  }
  else
  {
    random_num = temp;
  }
  return random_num;
}

void
  InitRandom (RANDOM_SEED_TYPE seed)
{
  ran_seed = -labs (seed);
  RANNUM;
}
