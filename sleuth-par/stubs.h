/*
 * stubs.h — as funcoes que spread.c consome e que, na arvore original, moram
 * em outros modulos do SLEUTH (igrid_obj, memory_obj, utilities, stats_obj,
 * coeff_obj, scenario_obj, proc_obj).
 *
 * Sao 25 simbolos. A lista de §6.3 do plano original tinha 23: faltavam
 * coeff_GetCurrentSlopeResist e scen_GetCriticalSlope, ambas usadas por
 * spr_get_slp_weights. Ver SPEC.md §B11.
 *
 * As assinaturas sao as dos cabecalhos originais, com uma diferenca: onde o
 * original escreve `()` (prototipo K&R, parametros nao especificados), aqui
 * esta `(void)`. Nao muda comportamento e deixa -Wstrict-prototypes limpo.
 *
 * As implementacoes estao em stubs.c (acesso a estado global) e util.c
 * (as cinco util_*, portadas de SLEUTH/utilities.c sem alteracao semantica).
 */
#ifndef STUBS_H
#define STUBS_H

/* ------------------------------------------------------------------------ *
 * SEIS SIMBOLOS FORAM ELIMINADOS PELA GLOBALIZACAO DAS MATRIZES
 *
 *   igrid_GetExcludedGridPtr      igrid_GetSlopeGridPtr
 *   igrid_GetRoadGridPtrByYear    igrid_GridRelease
 *   mem_GetWGridPtr               mem_GetWGridFree
 *
 * Todos existiam para uma coisa so: entregar a spr_spread um ponteiro para
 * uma grade e depois devolve-lo ao pool. Com as grades globais (grid.h) esse
 * emprestimo nao tem mais sentido — spr_spread le g_slp, g_excld, g_roads,
 * g_delta e g_workspace diretamente.
 *
 * Nao sao stubs vazios deixados para tras: sumiram junto com o conceito de
 * pool de grades. Dos 25 simbolos externos do §D1, restam 19.
 *
 * Efeito colateral util para o capitulo de paralelizacao: o pool do
 * memory_obj original e estado global mutavel com contador de emprestimos,
 * que precisaria de trava propria sob N threads. A globalizacao remove o
 * problema em vez de sincroniza-lo.
 * ------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------ *
 * igrid_obj / memory_obj — o que restou: dimensoes
 * ------------------------------------------------------------------------ */

int igrid_GetNumRows (void);
int igrid_GetNumCols (void);
int mem_GetTotalPixels (void);

/* ------------------------------------------------------------------------ *
 * utilities — portadas de SLEUTH/utilities.c
 * ------------------------------------------------------------------------ */

void util_init_grid (GRID_P gif, PIXEL value);
int  util_count_neighbors (GRID_P grid, int i, int j, int option, PIXEL value);
/*
 * As formas "_range" entraram no S05 (T3): operam em [lo, hi) em vez de na
 * grade inteira, que e tudo o que estas duas varreduras precisam para serem
 * repartidas entre threads. Ver util.c.
 */
void util_init_grid_range (GRID_P gif, PIXEL value, int lo, int hi);
void util_condition_gif_range (int lo, int hi, GRID_P source, int option,
                               int cmp_value, GRID_P target, int set_value);
/*
 * `cursor` entrou no estagio S04 (T5): era um `static int last_index` dentro
 * da funcao, e agora e propriedade do chamador. Ver util.c.
 */
void util_get_next_neighbor (int i_in, int j_in, int *i_out, int *j_out,
                             int index, int *cursor);
void util_condition_gif (int num_pixels, GRID_P source, int option,
                         int cmp_value, GRID_P target, int set_value);
int  util_count_pixels (int num_pixels, GRID_P pixels, int option, int value);

/* ------------------------------------------------------------------------ *
 * stats_obj — contadores de tentativa/sucesso de urbanizacao
 *
 * AS CINCO stats_Increment* SUMIRAM NO S05 (tarefa T5). Eram funcoes sem
 * argumento que incrementavam um `static stats_counters_t` do stubs.c — isto
 * e, estado global mutavel escrito de dentro de spr_urbanize, que e o ponto
 * mais quente do caminho paralelo. Sob N threads seriam corrida de dados
 * pura, e a invariante V5 e explicita: acumulador por thread, reducao na
 * barreira.
 *
 * Nao viraram versoes com trava: FORAM REMOVIDAS. Quem contabiliza agora e o
 * par_acc_t do worker (par.h), passado a spr_urbanize, e a soma dos N
 * acumuladores entra aqui uma vez por ano, por stats_Accumulate, chamada pela
 * thread que disparou o ano e fora de qualquer regiao paralela.
 *
 * Apagar em vez de sincronizar e o padrao do projeto, e nao coincidencia: o
 * S04 fez o mesmo com o `static last_index` de util_get_next_neighbor e com o
 * estado do ran_random (§B12 do mestre). Dos estados compartilhados
 * catalogados, a maioria se resolveu com projeto e nao com exclusao mutua.
 * Uma funcao que nao existe nao pode ser chamada por engano de dentro de uma
 * thread — garantia que nenhuma trava oferece.
 * ------------------------------------------------------------------------ */

/*
 * Os cinco contadores. O typedef subiu para ca no S05: antes ficava no fim do
 * arquivo, servindo so a inspecao e aos testes, e agora e o tipo do
 * acumulador por thread (par_acc_t o embute), entao precisa vir antes de
 * quem o usa.
 *
 * O delta_failure e o mais interessante para este trabalho: e exatamente a
 * leitura do read-modify-write que a paralelizacao torna insegura (§V6).
 */
typedef struct
{
  long urban_success;
  long z_failure;
  long delta_failure;
  long slope_failure;
  long excluded_failure;
} stats_counters_t;

/* soma os contadores de UM ano ao acumulado da corrida. Serial, 1x por ano. */
void stats_Accumulate (const stats_counters_t *ano);

void             stats_Reset (void);
stats_counters_t stats_Get (void);

/* ------------------------------------------------------------------------ *
 * coeff_obj / scenario_obj / proc_obj — parametros do ano corrente
 * ------------------------------------------------------------------------ */

double coeff_GetCurrentDiffusion (void);
double coeff_GetCurrentSpread (void);
double coeff_GetCurrentBreed (void);
double coeff_GetCurrentSlopeResist (void);
double coeff_GetCurrentRoadGravity (void);
double scen_GetCriticalSlope (void);
int    proc_GetCurrentYear (void);

/* ------------------------------------------------------------------------ *
 * Configuracao — nao existe no SLEUTH original (la os parametros vem do
 * arquivo de cenario e do modulo de calibracao). Aqui main.c injeta os
 * valores direto, ja que calibracao esta fora de escopo (SPEC.md §C).
 * ------------------------------------------------------------------------ */

void sim_SetCoefficients (double diffusion, double breed, double spread,
                          double slope_resist, double road_gravity);
void sim_SetCriticalSlope (double critical_slope);
void sim_SetCurrentYear (int year);

#endif /* STUBS_H */
