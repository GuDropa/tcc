/*
 * grid.h — as matrizes de dados do modelo, globais.
 *
 * Decisao de projeto do orientador (Esqueleto TCC, secao 3.2; plano §3.3):
 * as matrizes deixam de ser passadas funcao a funcao e passam a ser globais.
 * O motivo e pratico: a funcao que cada thread executa recebe so a faixa de
 * linhas que lhe cabe, sem precisar de uma struct de contexto com seis
 * ponteiros.
 *
 * Papel de cada uma no autômato:
 *
 *   g_z          estado urbano corrente.  LIDO pelas tres fases, escrito
 *                somente no merge ao final de spr_spread. E essa imutabilidade
 *                durante as fases que dispensa sincronizacao na leitura (V2).
 *   g_delta      buffer "proximo estado". Todo crescimento e escrito aqui.
 *                E o alvo da regiao critica (V6).
 *   g_slp        declividade   (entrada, somente leitura)
 *   g_excld      exclusao      (entrada, somente leitura)
 *   g_roads      malha viaria  (entrada, somente leitura)
 *   g_workspace  rascunho da fase 5 (lista de celulas em crescimento)
 *   g_urban_seed semente urbana do ano 0, preservada para a re-estampagem
 *                anual de growth.c:241-248 (estagio S03, tarefa T6)
 *
 * O double buffering z/delta ja existia no SLEUTH original — nao foi
 * introduzido por este trabalho. Ver SPEC.md §B2.
 *
 * As dimensoes g_nrows, g_ncols e g_total_pixels estao em globals.h porque
 * as macros OFFSET/IMAGE_PT/INTERIOR_PT dependem delas.
 */
#ifndef GRID_H
#define GRID_H

#include "globals.h"

extern GRID_P g_z;
extern GRID_P g_delta;
extern GRID_P g_slp;
extern GRID_P g_excld;
extern GRID_P g_roads;
extern GRID_P g_workspace;
extern GRID_P g_urban_seed;

/*
 * Aloca todas as grades de uma vez e fixa as dimensoes.
 * Chamada UMA VEZ, na inicializacao. Depois disso nao ha mais alocacao no
 * caminho de execucao — invariante V9.
 */
void grid_alloc (int nrows, int ncols);
void grid_free (void);

#endif /* GRID_H */
