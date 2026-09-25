/*
 * spread.h — ponto de entrada do nucleo.
 *
 * spr_spread simula UM ano: le g_z, escreve g_delta, e no fim funde delta em
 * z. Como o merge ja escreve em z, a realimentacao "saida do ano vira entrada
 * do ano seguinte" e automatica — main.c nao precisa trocar ponteiros.
 *
 * Os contadores de saida, um por regra de crescimento:
 *   sng  spontaneous          (fase 1)
 *   sdc  spreading centers    (fase 3)
 *   og   organic growth       (fase 4)
 *   rt   road trips           (fase 5)
 */
#ifndef SPREAD_H
#define SPREAD_H

#include "globals.h"

void spr_spread (float *average_slope,
                 int *num_growth_pix,
                 int *sng,
                 int *sdc,
                 int *og,
                 int *rt,
                 int *pop);

/*
 * ORDEM DA VARREDURA DA FASE 4 — instrumento de verificacao, estagio S04.
 *
 * Quando != 0, spr_phase4 varre as linhas de baixo para cima. Nao muda o
 * modelo: muda so a ORDEM em que as celulas sao visitadas.
 *
 * Existe para tornar VS17 — isto e, a invariante V4 — verificavel SEM
 * THREADS. O argumento e este: se o sorteio de uma celula e funcao pura da
 * chave dela, entao a ordem de visita nao pode influir no resultado, e inverter
 * as linhas tem de produzir um `z` identico. Se influir, falha tambem
 * paralelizado — mas ai a culpa seria atribuida a concorrencia, e o bug
 * viveria escondido atras de um mutex que nunca o resolveria.
 *
 * E por isso que este teste precisa existir ANTES de haver qualquer thread:
 * ele separa "dependencia de ordem" de "corrida de dados", que sao problemas
 * diferentes com solucoes diferentes (§N do spec do estagio).
 *
 * Em modo legacy a inversao DEVE mudar o resultado — o gerador e um fluxo
 * unico e a ordem de consumo e a ordem de varredura. VS17 verifica os dois
 * lados, porque um teste que passasse nos dois modos nao estaria testando
 * nada.
 */
extern int g_scan_reverse;

#endif /* SPREAD_H */
