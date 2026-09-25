/*
 * stubs.c — implementacoes minimas dos modulos do SLEUTH que spread.c
 * consome mas que estao fora do escopo deste trabalho.
 *
 * Ver stubs.h para a lista e para os seis simbolos que a globalizacao das
 * matrizes eliminou.
 *
 * O que cada modulo original fazia, e o que sobrou aqui:
 *
 *   igrid_obj    lia GIFs de entrada, mantinha pilha de grades emprestadas.
 *                Sobrou: as dimensoes.
 *   memory_obj   arena propria com log de alocacao. Sobrou: total de pixels.
 *   coeff_obj    coeficientes correntes, incluindo a automodificacao que os
 *                reajusta entre anos conforme a taxa de crescimento.
 *                Sobrou: valores fixos injetados por main.c.
 *   scenario_obj parser do arquivo de cenario. Sobrou: um valor.
 *   proc_obj     controle de anos, Monte Carlo e conjuntos de coeficientes.
 *                Sobrou: o ano corrente.
 *   stats_obj    dezenas de metricas de calibracao (Lee-Sallee etc).
 *                Sobrou: os cinco contadores que spr_urbanize alimenta.
 *
 * ATENCAO (SPEC.md §V5): os contadores abaixo sao `long` simples. Sao
 * suficientes enquanto a execucao for serial. Sob N threads viram corrida de
 * dados — a versao com acumulador por thread e reducao na barreira e do
 * estagio S06.
 */

#include "globals.h"
#include "grid.h"

/* ------------------------------------------------------------------------ *
 * dimensoes
 * ------------------------------------------------------------------------ */

int igrid_GetNumRows (void)   { return g_nrows; }
int igrid_GetNumCols (void)   { return g_ncols; }
int mem_GetTotalPixels (void) { return g_total_pixels; }

/* ------------------------------------------------------------------------ *
 * coeficientes e cenario
 * ------------------------------------------------------------------------ */

static double cur_diffusion;
static double cur_breed;
static double cur_spread;
static double cur_slope_resist;
static double cur_road_gravity;
static double cur_critical_slope;
static int    cur_year;

void
  sim_SetCoefficients (double diffusion, double breed, double spread,
                       double slope_resist, double road_gravity)
{
  cur_diffusion    = diffusion;
  cur_breed        = breed;
  cur_spread       = spread;
  cur_slope_resist = slope_resist;
  cur_road_gravity = road_gravity;
}

void sim_SetCriticalSlope (double critical_slope)
{
  cur_critical_slope = critical_slope;
}

void sim_SetCurrentYear (int year) { cur_year = year; }

double coeff_GetCurrentDiffusion (void)    { return cur_diffusion; }
double coeff_GetCurrentBreed (void)        { return cur_breed; }
double coeff_GetCurrentSpread (void)       { return cur_spread; }
double coeff_GetCurrentSlopeResist (void)  { return cur_slope_resist; }
double coeff_GetCurrentRoadGravity (void)  { return cur_road_gravity; }
double scen_GetCriticalSlope (void)        { return cur_critical_slope; }
int    proc_GetCurrentYear (void)          { return cur_year; }

/* ------------------------------------------------------------------------ *
 * contadores de tentativa de urbanizacao
 *
 * spr_urbanize testa quatro condicoes em cascata e incrementa exatamente um
 * destes contadores por chamada:
 *
 *   z_failure         a celula ja era urbana
 *   delta_failure     outra regra ja urbanizou a celula neste mesmo ano
 *   slope_failure     reprovada no teste de declividade
 *   excluded_failure  caiu em area excluida
 *   urban_success     urbanizou
 *
 * ATE O S04 spr_urbanize incrementava ESTE bloco diretamente, por cinco
 * funcoes sem argumento. Nao incrementa mais: cada worker conta no proprio
 * par_acc_t e a soma do ano chega aqui por stats_Accumulate. Ver a nota em
 * stubs.h sobre por que as cinco funcoes foram apagadas em vez de ganharem
 * trava.
 *
 * O que sobrou aqui e, portanto, estado global escrito UMA VEZ POR ANO, pela
 * thread que disparou o ano, depois do join. Isso nao e corrida de dados
 * coisa nenhuma — e o mesmo padrao dos coeficientes logo acima.
 * ------------------------------------------------------------------------ */

static stats_counters_t counters;

void
  stats_Accumulate (const stats_counters_t *ano)
{
  counters.urban_success    += ano->urban_success;
  counters.z_failure        += ano->z_failure;
  counters.delta_failure    += ano->delta_failure;
  counters.slope_failure    += ano->slope_failure;
  counters.excluded_failure += ano->excluded_failure;
}

void stats_Reset (void)
{
  counters.urban_success    = 0;
  counters.z_failure        = 0;
  counters.delta_failure    = 0;
  counters.slope_failure    = 0;
  counters.excluded_failure = 0;
}

stats_counters_t stats_Get (void) { return counters; }
