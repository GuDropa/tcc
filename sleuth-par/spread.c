/*
 * spread.c — nucleo de simulacao do SLEUTH, adaptado para matrizes globais.
 *
 * Derivado de spread-simplified.c (enviado pelo orientador), que por sua vez
 * e um recorte de SLEUTH/spread.c sem I/O, calibracao e interpolacao.
 *
 * MUDANCA DESTE COMMIT: as grades deixaram de ser parametros.
 *
 * Antes:  spr_phase4 (spread_coefficient, z, excld, delta, slp, swght, og)
 * Agora:  spr_phase4 (spread_coefficient, swght, og)
 *
 * Cada funcao que usava as grades abre com um bloco de aliases locais:
 *
 *     GRID_P z = g_z, delta = g_delta;
 *
 * Isso mantem os CORPOS byte a byte identicos ao original — o que importa
 * para a invariante V7 (serial == SLEUTH original) e deixa o diff legivel —
 * ao mesmo tempo em que atende a orientacao de usar matrizes globais, porque
 * a funcao que a thread vai executar nao precisa mais receber seis ponteiros.
 * Os aliases sao ponteiros locais; o compilador os elimina.
 *
 * Ver SPEC.md §C, grid.h, e o plano §3.3.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include "globals.h"
#include "grid.h"
#include "par.h"


#define SPREAD_MODULE
#define SWGHT_TYPE float
#define SLOPE_WEIGHT_ARRAY_SZ 256

/* ver spread.h: instrumento de VS17, invariante V4 */
int g_scan_reverse = 0;

/* ------------------------------------------------------------------------ *
 * SONDA DE REPARTICAO DE TEMPO ENTRE AS FASES — compilar com
 * -DPROBE_PHASE_TIMING. Fora do build normal.
 *
 * Existe para responder, com numero em vez de intuicao, a pergunta que o
 * spec do S05 precisa responder antes de ser escrito: QUANTO do tempo esta na
 * fase 4? E o teto de Amdahl do estagio — paralelizar perfeitamente uma fase
 * que vale 60 % do relogio limita o ganho a 2,5x, por mais threads que se
 * jogue nela.
 *
 * Seguindo o mesmo padrao do -DSLEUTH_ORIGINAL_B7_BUG acima: demonstrar em
 * vez de afirmar, sem custo nenhum no binario de producao.
 * ------------------------------------------------------------------------ */
#ifdef PROBE_PHASE_TIMING
#include <stdio.h>
#include <time.h>

/*
 * Sete slots, e o primeiro e o que o S05 descobriu que precisava existir: o
 * custo de MONTAR o paralelismo, separado do custo de executa-lo.
 *
 * Sem ele, o pthread_create das N-1 threads caia dentro de "zerar delta" — a
 * primeira etapa do ano — e a leitura ficava "zerar delta ficou 25x mais
 * lento com 8 threads", que e falso e confunde duas coisas diferentes: se as
 * varreduras repartem trabalho, e se o pool se paga. Ver §B22.
 *
 * ERAM OITO ate o S06 (T1). O oitavo era "pop", a contagem de populacao, e
 * ele sumiu porque a passada que ele media sumiu: a contagem foi fundida no
 * laco do merge (§D6 do spec do S06). Nao se substituiu por um slot de ~0,00
 * ms — um rotulo que nomeia uma etapa inexistente e pior que uma coluna a
 * menos, e este projeto ja tropecou tres vezes em "a sonda mede o que alguem
 * lembrou de marcar" (§B24). O ganho do T1 se le no "total" e no "merge".
 */
#define PROBE_SLOTS 7   /* pool, zera-delta, 1n3, 4, 5, filtros, merge */

/*
 * O cursor era uma variavel local de spr_spread, declarada pela propria
 * PROBE_INICIO. Passou a ser estatico no estagio S05 porque as marcacoes
 * mudaram de funcao: agora vivem em spr_year_body, que e o que as threads
 * rodam, enquanto a ultima (o fecho do merge) fica em spr_spread, depois do
 * join. Ninguem alem do worker 0 encosta nisto — toda marcacao e guardada
 * por `id == 0`.
 *
 * O que a sonda mede sob N threads e o RELOGIO DE PAREDE de cada etapa, ja
 * que as marcas ficam depois das barreiras e portanto incluem a espera do
 * worker 0 pelos demais. E o numero certo: o que interessa e quanto o ano
 * demora, nao quanto cada thread trabalhou.
 */
static double probe_acc[PROBE_SLOTS];
static double probe_t0;
static long   probe_anos;

static double
  probe_agora (void)
{
  struct timespec t;
  clock_gettime (CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}

void
  probe_phase_report (void)
{
  static const char *rot[PROBE_SLOTS] = {"pool", "zera delta", "fase 1n3",
                                         "fase 4", "fase 5", "filtros",
                                         "merge"};
  double total = 0.0;
  int i;

  for (i = 0; i < PROBE_SLOTS; i++) total += probe_acc[i];
  fprintf (stderr, "\n-- reparticao do tempo em spr_spread, %ld ano(s), "
                   "%d thread(s) --\n", probe_anos, g_threads);
  for (i = 0; i < PROBE_SLOTS; i++)
    fprintf (stderr, "   %-10s %9.2f ms  %6.2f %%\n",
             rot[i], probe_acc[i] * 1e3, 100.0 * probe_acc[i] / total);
  fprintf (stderr, "   %-10s %9.2f ms\n", "total", total * 1e3);
}

#define PROBE_INICIO()      do { probe_t0 = probe_agora (); } while (0)
#define PROBE_MARCA(slot)   do { double _t = probe_agora ();            \
                                 probe_acc[slot] += _t - probe_t0;      \
                                 probe_t0 = _t; } while (0)
/* o relatorio se registra sozinho no primeiro ano — main.c nao sabe da sonda */
#define PROBE_ANO()         do { if (probe_anos++ == 0)                 \
                                   atexit (probe_phase_report); } while (0)
#else
#define PROBE_INICIO()      do { } while (0)
#define PROBE_MARCA(slot)   do { } while (0)
#define PROBE_ANO()         do { } while (0)
#endif


void     spr_spiral (int index,
                int *i_out,
                int *j_out);

/*
 * ESTAGIO S05, TAREFA T5 — as quatro fases e spr_urbanize ganharam um
 * `par_acc_t *acc`: o acumulador DA THREAD que esta executando.
 *
 * Ate aqui os cinco contadores de tentativa viviam num bloco estatico do
 * stubs.c, incrementado de dentro de spr_urbanize por funcoes sem argumento.
 * Serial, funcionava; sob N threads e corrida de dados no ponto mais quente
 * do programa. V5 nao admite: acumulador por thread, reducao na barreira.
 *
 * `og` migrou junto e pelo mesmo motivo — e o unico dos quatro contadores de
 * crescimento alimentado por uma fase PARALELA. `sng`, `sdc` e `rt` continuam
 * `int *` porque as fases 1n3 e 5 rodam so no worker 0 (§D7): dar-lhes
 * tratamento de acumulador sugeriria uma concorrencia que nao existe.
 */
void     spr_phase1n3 (COEFF_TYPE diffusion_coefficient,
                  COEFF_TYPE breed_coefficient,
                  SWGHT_TYPE * swght,
                  int *sng,
                  int *sdc,
                  par_acc_t * acc);

/*
 * A fase 4 recebe o WORKER e nao so o acumulador: e a unica que precisa da
 * faixa de linhas, porque e a unica que varre a grade olhando vizinhanca.
 */
void     spr_phase4 (COEFF_TYPE spread_coefficient,
                SWGHT_TYPE * swght,
                par_worker_t * w);


void     spr_phase5 (COEFF_TYPE road_gravity,
                COEFF_TYPE diffusion_coefficient,
                COEFF_TYPE breed_coefficient,
                SWGHT_TYPE * swght,
                int *rt,
                par_acc_t * acc);

void     spr_get_slp_weights (int array_size,
                         SWGHT_TYPE * lut);

static bool spr_road_search (int i_grwth_center,
                                int j_grwth_center,
                                int *i_road,
                                int *j_road,
                                int max_search_index);

/*
 * ESTAGIO S04, TAREFA T3 — as quatro assinaturas abaixo ganharam um
 * `rng_ctx_t ctx`.
 *
 * O contexto e a coordenada determinista do trabalho que MOTIVOU a chamada:
 * (fase, a, b). Estas funcoes deixaram de DEDUZIR a chave dos proprios
 * argumentos e passaram a RECEBE-LA. A diferenca e o estagio inteiro.
 *
 * Por que deduzir nao serve, com o caso concreto: spr_urbanize recebe a
 * celula-ALVO. Se ela ancorasse o sorteio em (row, col), duas origens
 * diferentes que tentassem urbanizar o mesmo alvo no mesmo ano receberiam
 * sorteios IDENTICOS, onde o original faria duas tentativas independentes. E
 * ha um caso pior na fase 5: o laco de `tries` de spr_phase5 chama
 * spr_urbanize_nghbr tres vezes com argumentos identicos, contando com o
 * sorteio para variar — ancorado no alvo, as tres ficariam iguais e o laco
 * viraria no-op. Dai o `b` do contexto ser o indice da TENTATIVA.
 *
 * Ver §D8 do spec do estagio e a tabela de subpassos em rng.h.
 */
static bool spr_road_walk (int i_road_start,
                         int j_road_start,
                         int *i_road_end,
                         int *j_road_end,
                         double diffusion_coefficient,
                         rng_ctx_t ctx);

static bool spr_urbanize_nghbr (int i,
                              int j,
                              int *i_nghbr,
                              int *j_nghbr,
                              SWGHT_TYPE * swght,
                              PIXEL pixel_value,
                              int *stat,
                              rng_ctx_t ctx,
                              par_acc_t * acc);

static void spr_get_neighbor (int i_in,
                         int j_in,
                         int *i_out,
                         int *j_out,
                         rng_ctx_t ctx);

bool    spr_urbanize (int row,
                  int col,
                  SWGHT_TYPE * swght,
                  PIXEL pixel_value,
                  int *stat,
                  rng_ctx_t ctx,
                  par_acc_t * acc);

COEFF_TYPE     spr_GetDiffusionValue (COEFF_TYPE diffusion_coeff);    /* IN    */
COEFF_TYPE     spr_GetRoadGravValue (COEFF_TYPE rg_coeff);            /* IN    */



/*
 * Fases 1 e 3 do artigo: crescimento espontaneo e novo centro difusor.
 *
 * Nao varre a matriz: sorteia 1 + diffusion_value celulas em qualquer lugar
 * da grade. Nao e particionavel por faixas de linhas — o que se divide entre
 * as threads e o laco de iteracoes (SPEC.md, plano §4.4).
 *
 * Nao precisa de aliases: nao toca nas grades diretamente, so repassa.
 */
void   spr_phase1n3 (COEFF_TYPE diffusion_coefficient,
                COEFF_TYPE breed_coefficient,
                SWGHT_TYPE * swght,
                int *sng,
                int *sdc,
                par_acc_t * acc)
{
  int i;
  int j;
  int i_out;
  int j_out;
  int k;
  int count;
  int tries;
  int max_tries;
  COEFF_TYPE diffusion_value;
  bool urbanized;


  diffusion_value = spr_GetDiffusionValue (diffusion_coefficient);

  for (k = 0; k < 1 + (int) diffusion_value; k++)
  {
    /*
     * §D4 do spec: esta fase NAO varre a grade — ela sorteia a celula. Nao ha
     * celula antes do sorteio, entao a ancora nao pode ser (i,j); e o indice
     * `k` da iteracao. O laco tem contagem fixa (1 + diffusion_value), logo
     * `k` e uma coordenada determinista do trabalho, que e o que V4 exige.
     */
    rng_ctx_t ctx = RNG_CTX (RNG_FASE_1, k, 0);

    i = RNG_ROW (ctx);
    j = RNG_COL (ctx);

    if (INTERIOR_PT (i, j))
    {
      if (spr_urbanize (i,
                        j,
                        swght,
                        PHASE1G,
                        sng,
                        ctx,
                        acc))
      {
        if (rng_next_int (ctx, RNG_SUB_BREED, 101) < (int) breed_coefficient)
        {
          count = 0;
          max_tries = 8;
          for (tries = 0; tries < max_tries; tries++)
          {
            /*
             * Fase 3 (novo centro difusor), ancorada em (k, tries). O indice
             * da tentativa e OBRIGATORIO: as oito chamadas abaixo recebem os
             * mesmos i,j e contam com o sorteio de spr_get_neighbor para
             * visitar vizinhos diferentes. Sem `tries` na chave, as oito
             * seriam identicas.
             */
            rng_ctx_t ctx3 = RNG_CTX (RNG_FASE_3, k, tries);

            urbanized = FALSE;
            urbanized =
              spr_urbanize_nghbr (i,
                                  j,
                                  &i_out,
                                  &j_out,
                                  swght,
                                  PHASE3G,
                                  sdc,
                                  ctx3,
                                  acc);
            if (urbanized)
            {
              count++;
              if (count == MIN_NGHBR_TO_SPREAD)
              {
                break;
              }
            }
          }
        }
      }
    }
  }

}



/*
 * Fase 4 do codigo = "organic growth" do artigo.
 *
 * E a unica fase que varre a grade inteira, e a unica verdadeiramente
 * data-parallel: o duplo laco row/col divide-se em faixas de linhas sem
 * nenhuma dependencia entre elas, porque z e somente leitura (V2). E por
 * onde a paralelizacao comeca, em S05.
 *
 * Divergencia em relacao ao artigo (SPEC.md §V10): a Figura 4 de Clarke,
 * Hoppen & Gaydos (1997) diz "for all cells with at least THREE neighbors";
 * o codigo testa urb_count >= 2 && urb_count < 8, ou seja, DOIS vizinhos
 * bastam e celulas totalmente cercadas ficam de fora. O codigo manda.
 */
void   spr_phase4 (COEFF_TYPE spread_coefficient,
              SWGHT_TYPE * swght,
              par_worker_t * w)
{
  GRID_P z = g_z;
  par_acc_t *acc = &w->acc;

  int r;
  int r_lo;
  int r_hi;
  int row;
  int col;
  int row_nghbr;
  int col_nghbr;
  int pixel;
  int walkabout_row[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
  int walkabout_col[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
  int urb_count;
  int nrows;
  int ncols;

  nrows = igrid_GetNumRows ();
  ncols = igrid_GetNumCols ();
  assert (nrows > 0);
  assert (ncols > 0);

  /*
   * ESTAGIO S05, TAREFA T6 — a faixa desta thread, interceptada com o
   * interior da grade.
   *
   * A varredura sempre foi de 1 a nrows-2: util_count_neighbors nao confere
   * borda e o chamador e que tem de passar ponto interior. Com faixas, o
   * recorte e o mesmo feito duas vezes — a faixa do worker e o interior — e o
   * MAX/MIN abaixo e a intersecao. O worker que pegou a primeira faixa perde
   * a linha 0, o que pegou a ultima perde a linha nrows-1, e os do meio
   * ficam inteiros.
   *
   * Faixa vazia (r_lo >= r_hi) e legitima: acontece com mais threads do que
   * linhas, e o laco simplesmente nao executa.
   */
  r_lo = MAX (w->row_lo, 1);
  r_hi = MIN (w->row_hi, nrows - 1);

  for (r = r_lo; r < r_hi; r++)
  {
    /*
     * A ordem de visita, e SO ela, muda quando g_scan_reverse esta ligado:
     * r percorre a faixa sempre, e `row` e ele ou o seu espelho. O corpo
     * abaixo nao sabe a diferenca. Ver spread.h e VS17.
     *
     * Com N threads o espelho TROCA as faixas de dono — o worker que varria
     * as linhas de cima passa a varrer as de baixo — mas continua sendo uma
     * particao: as faixas espelhadas tambem cobrem [1, nrows-1) sem buraco e
     * sem sobreposicao, e cada uma continua contigua, que e o que o halo de
     * §D3 exige. VS17 continua valendo palavra por palavra sob threads.
     */
    row = g_scan_reverse ? (nrows - 1 - r) : r;

    for (col = 1; col < ncols - 1; col++)
    {
      /*
       * A ancora desta fase e a celula varrida — o unico caso em que "a
       * celula" de V4 e literal. E a chave da ORIGEM: ela viaja com a chamada
       * a spr_urbanize abaixo, que urbaniza um VIZINHO.
       */
      rng_ctx_t ctx = RNG_CTX (RNG_FASE_4, row, col);

      /*
       * O curto-circuito de `&&` continua igual ao original, mas mudou de
       * natureza. Antes ele era o mecanismo que QUEBRAVA V3: celula nao urbana
       * nao sorteava, entao quantos sorteios foram consumidos antes de chegar
       * a (row,col) dependia do estado urbano de todas as celulas anteriores
       * na ordem de varredura (§D2 — 0,134 sorteio por celula varrida). Com o
       * sorteio ancorado, pular a chamada nao desloca fluxo nenhum: o valor
       * desta celula e funcao da chave dela e de mais nada. O curto-circuito
       * virou o que aparentava ser — so economia de trabalho.
       */
      if ((z[OFFSET (row, col)] > 0) &&
          (rng_next_int (ctx, RNG_SUB_SPREAD, 101) < spread_coefficient))
      {

        urb_count = util_count_neighbors (z, row, col, GT, 0);
        if ((urb_count >= 2) && (urb_count < 8))
        {
          pixel = rng_next_int (ctx, RNG_SUB_PIXEL, 8);

          row_nghbr = row + walkabout_row[pixel];
          col_nghbr = col + walkabout_col[pixel];

          spr_urbanize (row_nghbr,
                        col_nghbr,
                        swght,
                        PHASE4G,
                        &acc->og,
                        ctx,
                        acc);
        }
      }
    }
  }

}

/*
 * Fase 5 = "road influenced growth" do artigo.
 *
 * Monta a lista das celulas que cresceram neste ano, sorteia 1 + breed
 * delas, procura uma estrada por perto, caminha pela malha viaria e urbaniza
 * LONGE da origem. E o pior caso para a paralelizacao: as escritas sao
 * arbitrarias em posicao, entao nenhum particionamento espacial ajuda
 * (SPEC.md, plano §4.4).
 */
void   spr_phase5 (COEFF_TYPE road_gravity,
              COEFF_TYPE diffusion_coefficient,
              COEFF_TYPE breed_coefficient,
              SWGHT_TYPE * swght,
              int *rt,
              par_acc_t * acc)
{
  GRID_P delta     = g_delta;
  GRID_P workspace = g_workspace;

  int iii;
  int int_road_gravity;
  int growth_count;
  int *growth_row;
  int *growth_col;
  int max_search_index;
  int growth_index;
  bool road_found;
  int i_rd_start;
  int j_rd_start;
  int max_tries;
  bool spread;
  bool urbanized;
  int i_rd_end;
  int j_rd_end;
  int i_rd_end_nghbr;
  int j_rd_end_nghbr;
  int i_rd_end_nghbr_nghbr;
  int j_rd_end_nghbr_nghbr;
  int tries;
  int nrows;
  int ncols;
  int total_pixels;


  nrows = igrid_GetNumRows ();
  ncols = igrid_GetNumCols ();



  total_pixels = mem_GetTotalPixels ();

  /*
   * CORRECAO DE BUG DO SLEUTH ORIGINAL — ver SPEC.md §B7.
   *
   * O original escreve:
   *
   *     growth_row = (int *) workspace;
   *     growth_col = (int *) workspace + (nrows);      <-- aqui
   *
   * growth_col comeca `nrows` inteiros depois de growth_row, mas o laco
   * abaixo pode empilhar ate `total_pixels` entradas em growth_row. Assim
   * que growth_count passa de nrows, growth_row[] comeca a sobrescrever
   * growth_col[], e a fase 5 passa a sortear coordenadas corrompidas.
   *
   * Numa grade 20x20 bastam 21 celulas em crescimento para disparar.
   * Confirmado identico em spread-simplified.c:281 e SLEUTH/spread.c:408,
   * ou seja: e do SLEUTH, nao artefato da simplificacao.
   *
   * O deslocamento correto e total_pixels. O tamanho do bloco ja era
   * suficiente (uma grade de total_pixels longs = 2*total_pixels ints), e o
   * _Static_assert de globals.h garante que continue sendo — ele mora la, ao
   * lado da definicao de PIXEL, porque e ela que pode invalidar a conta (§B8).
   */
  growth_row = (int *) workspace;
#ifdef SLEUTH_ORIGINAL_B7_BUG
  /*
   * Compilar com -DSLEUTH_ORIGINAL_B7_BUG restaura o deslocamento do SLEUTH
   * original. Serve para demonstrar o bug em vez de so afirma-lo: basta rodar
   * uma grade em que growth_count passe de nrows e comparar as duas saidas.
   * Ver tests/run_checks.sh.
   */
  growth_col = (int *) workspace + nrows;
#else
  growth_col = (int *) workspace + total_pixels;
#endif

  growth_count = 0;

  for (iii = 0; iii < total_pixels; iii++)
  {
    if (delta[iii] > 0)
    {
      growth_row[growth_count] = iii / ncols;
      growth_col[growth_count] = iii % ncols;
      growth_count++;
    }
  }

  /*
   * A outra metade da garantia do workspace (§B8, tarefa T10). O
   * _Static_assert de globals.h diz que o bloco COMPORTA duas listas de
   * total_pixels ints; esta asercao diz que nunca empilhamos mais do que
   * isso. Juntas, cobrem o carve-out inteiro.
   *
   * Hoje o limite vale por construcao — o laco acima itera total_pixels vezes
   * e incrementa no maximo uma vez por iteracao. Fica escrito mesmo assim
   * porque e essa a premissa que o deslocamento `+ total_pixels` assume, e
   * porque S05/S06 vao repartir este laco entre threads: com acumuladores por
   * faixa e reducao, o limite deixa de ser obvio a olho nu.
   *
   * Vale nas duas compilacoes: o B7 e erro de DESLOCAMENTO, nao de contagem,
   * entao -DSLEUTH_ORIGINAL_B7_BUG continua passando por aqui — o que e
   * necessario, senao o check B7 de run_checks.sh abortaria em vez de produzir
   * o despejo divergente que ele compara.
   */
  assert (growth_count <= total_pixels);

  if (growth_count > 0)
  {
    for (iii = 0; iii < 1 + (int) (breed_coefficient); iii++)
    {
      /*
       * Como a fase 1n3 e pelo mesmo motivo (§D4): esta fase sorteia um
       * indice na lista de crescimento, entao nao ha celula antes do sorteio
       * e a ancora e o indice `iii` da iteracao, de contagem fixa.
       *
       * O `b` = 0 e o trabalho da propria iteracao (o sorteio do indice e a
       * caminhada pela estrada). As urbanizacoes que vem depois usam b = 1 e
       * b = 2+tries — ver abaixo.
       */
      rng_ctx_t ctx = RNG_CTX (RNG_FASE_5, iii, 0);

      int_road_gravity = spr_GetRoadGravValue (road_gravity);
      max_search_index = 4 * (int_road_gravity * (1 + int_road_gravity));
      max_search_index = MAX (max_search_index, nrows);
      max_search_index = MAX (max_search_index, ncols);


      growth_index =
        (int) ((double) growth_count * rng_next (ctx, RNG_SUB_GROWTH));


      road_found =
        spr_road_search (growth_row[growth_index],
                         growth_col[growth_index],
                         &i_rd_start,
                         &j_rd_start,
                         max_search_index);


      if (road_found)
      {
        spread = spr_road_walk (i_rd_start,
                                j_rd_start,
                                &i_rd_end,
                                &j_rd_end,
                                diffusion_coefficient,
                                ctx);

        if (spread == TRUE)
        {
          urbanized =
            spr_urbanize_nghbr (i_rd_end,
                                j_rd_end,
                                &i_rd_end_nghbr,
                                &j_rd_end_nghbr,
                                swght,
                                PHASE5G,
                                rt,
                                RNG_CTX (RNG_FASE_5, iii, 1),
                                acc);
          if (urbanized)
          {
            max_tries = 3;
            for (tries = 0; tries < max_tries; tries++)
            {
              /*
               * ############ O CASO PERIGOSO DO ESTAGIO ############
               *
               * Repare que as tres chamadas abaixo recebem EXATAMENTE os
               * mesmos argumentos: o laco nunca atualiza i_rd_end_nghbr, so
               * escreve em i_rd_end_nghbr_nghbr, que e descartado. No
               * original elas diferem unicamente porque spr_get_neighbor
               * sorteia um vizinho a cada vez.
               *
               * Ancorar a chave nos argumentos — que e a leitura ingenua de
               * "ancorado a celula" — faria as tres tentativas produzirem
               * sorteios bit a bit identicos, e o laco viraria um no-op
               * silencioso: mesmo vizinho, mesma decisao, tres vezes. O
               * modelo perderia duas das tres tentativas de crescimento por
               * estrada e nada acusaria, porque o programa continuaria
               * perfeitamente deterministico.
               *
               * Por isso `tries` entra na chave. O deslocamento 2+ deixa
               * b = 0 para a caminhada e b = 1 para o primeiro vizinho.
               */
              urbanized =
                spr_urbanize_nghbr (i_rd_end_nghbr,
                                    j_rd_end_nghbr,
                                    &i_rd_end_nghbr_nghbr,
                                    &j_rd_end_nghbr_nghbr,
                                    swght,
                                    PHASE5G,
                                    rt,
                                    RNG_CTX (RNG_FASE_5, iii, 2 + tries),
                                    acc);

            }
          }
        }
      }
    }
  }

}


void   spr_get_slp_weights (int array_size,                       
                       SWGHT_TYPE * lut)                   
{

  float val;
  float exp;
  int i;

  exp = coeff_GetCurrentSlopeResist () / (MAX_SLOPE_RESISTANCE_VALUE / 2.0);
  for (i = 0; i < array_size; i++)
  {
    if (i < scen_GetCriticalSlope ())
    {
      val = (scen_GetCriticalSlope () - (SWGHT_TYPE) i) / scen_GetCriticalSlope ();
      lut[i] = 1.0 - pow (val, exp);
    }
    else
    {
      lut[i] = 1.0;
    }
  }
}




COEFF_TYPE
  spr_GetDiffusionValue (COEFF_TYPE diffusion_coeff)
{

  COEFF_TYPE diffusion_value;
  double rows_sq;
  double cols_sq;

  rows_sq = igrid_GetNumRows () * igrid_GetNumRows ();
  cols_sq = igrid_GetNumCols () * igrid_GetNumCols ();

  /*
   * diffusion_value's MAXIMUM (IF diffusion_coeff == 100)
   * WILL BE 5% OF THE IMAGE DIAGONAL. 
   */

  diffusion_value = ((diffusion_coeff * 0.005) * sqrt (rows_sq + cols_sq));
  return diffusion_value;
}


COEFF_TYPE  spr_GetRoadGravValue (COEFF_TYPE rg_coeff)
{

  int rg_value;
  int row;
  int col;

  row = igrid_GetNumRows ();
  col = igrid_GetNumCols ();

  /*
   * rg_value's MAXIMUM (IF rg_coeff == 100)
   * WILL BE 1/16 OF THE IMAGE DIMENSIONS. 
   */

  rg_value = (rg_coeff / MAX_ROAD_VALUE) * ((row + col) / 16.0);

  return rg_value;
}


/*
 * spr_urbanize — a urbanizacao propriamente dita. Todo crescimento, de todas
 * as fases, passa por aqui.
 *
 * ############ E ESTA A REGIAO CRITICA DO TRABALHO (SPEC.md §V6) ############
 *
 * O par abaixo e um read-modify-write classico:
 *
 *     if (delta[OFFSET (row, col)] == 0)        <-- le
 *         ...
 *         delta[OFFSET (row, col)] = pixel_value;   <-- escreve
 *
 * Duas threads podem observar 0 ao mesmo tempo, ambas passar nos testes de
 * declividade e exclusao, e ambas escrever — contabilizando DOIS crescimentos
 * onde houve apenas um. O valor final em delta e quase inofensivo (as duas
 * gravam um pixel_value, talvez com tags de fase diferentes), mas *stat e os
 * contadores stats_Increment* ficam corrompidos. E como esses contadores
 * alimentam a automodificacao de coeficientes do SLEUTH, o erro se propaga
 * para os anos seguintes.
 *
 * A regiao critica e pequena — um bloco de quatro testes encadeados — e e
 * justamente por isso que da para implementar e comparar tres estrategias de
 * exclusao mutua (mutex global, sharding de locks, CAS atomico) sem
 * reescrever o modelo. Ver SPEC.md §V6; implementacao em S06.
 *
 * Detalhe de tipo que importa para a variante C (SPEC.md §B4): delta e
 * GRID_P, isto e, `long *`. O CAS tem de operar sobre PIXEL/long, nao sobre
 * unsigned char.
 */
bool
  spr_urbanize (int row,
                int col,
                SWGHT_TYPE * swght,
                PIXEL pixel_value,
                int *stat,
                rng_ctx_t ctx,
                par_acc_t * acc)
{
  GRID_P z     = g_z;
  GRID_P delta = g_delta;
  GRID_P slp   = g_slp;
  GRID_P excld = g_excld;

  const int off = OFFSET (row, col);

  bool val;

  val = FALSE;
  if (z[off] == 0)
  {
    /*
     * O teste de `z` fica FORA da exclusao de proposito: `z` e imutavel
     * durante as fases (V2), entao le-lo nao disputa com ninguem. Proteger
     * aqui alargaria a regiao critica sem comprar seguranca nenhuma.
     *
     * ############ A REGIAO CRITICA COMECA AQUI (V6) ############
     *
     * As quatro chamadas par_cell_* sao a interface unica das tres variantes
     * (§D4 do spec do estagio, e o comentario longo em par.h). Repare que
     * NENHUMA delas se chama "lock": duas das tres variantes trancam alguma
     * coisa, e a terceira — o CAS — nao tranca nada; ela toma a celula ou
     * descobre que a perdeu. A interface exprime a OPERACAO sobre a celula,
     * que e o que as tres tem em comum.
     *
     * Repare tambem no tamanho do que esta protegido: a leitura de delta, os
     * dois sorteios e a escrita. Os sorteios sao funcao pura da chave e nao
     * precisariam de protecao, mas precisam estar DENTRO dela — tira-los para
     * fora mudaria qual contador cada tentativa incrementa e portanto a saida
     * serial, que e o oraculo (VS21).
     *
     * E este o desperdicio que o estagio mede: sob A, o mutex global
     * serializa 100 % das urbanizacoes para proteger as ~4 % que caem nas
     * duas linhas de fronteira de cada faixa (§D3 do S05), e a conta sai em
     * +126 % na fase 4 com 8 threads (§D3 deste spec).
     *
     * Com uma thread as quatro degeneram no codigo serial de sempre.
     */
    par_cell_enter (off);

    if (par_cell_read (delta, off) == 0)
    {
      /*
       * Os dois sorteios desta funcao usam a chave da ORIGEM, recebida em
       * `ctx`, e nunca (row, col) — que aqui e o ALVO. Ver a nota das
       * assinaturas no topo do arquivo e §D8 do spec.
       *
       * E por serem ancorados na origem que a ordem de chegada nao muda o
       * resultado do MODELO: duas origens que disputem o mesmo alvo recebem
       * cada uma o seu sorteio, independentemente de quem entrou primeiro.
       */
      if (rng_next (ctx, RNG_SUB_SLOPE) > swght[slp[off]])
      {
        if (excld[off] < rng_next_int (ctx, RNG_SUB_EXCLD, 100))
        {
          if (par_cell_commit (delta, off, pixel_value))
          {
            val = TRUE;
            (*stat)++;
            acc->stats.urban_success++;
          }
          else
          {
            /*
             * ############ O RAMO MORTO SOB A E SOB B ############
             *
             * Sob A e B este else nunca executa: a trava garantiu a exclusao
             * antes da leitura de delta, entao o commit nao pode falhar. Sob
             * C ele e o caminho NORMAL da disputa perdida — a leitura la em
             * cima era uma dica, e entre ela e aqui outra thread tomou a
             * celula.
             *
             * Conta delta_failure, e e o mesmo contador do else de baixo de
             * proposito: as duas coisas sao "achei a celula ja tomada", e a
             * unica diferenca e o instante em que se descobriu. §D5 mostra
             * que a invariante que VS20/VS25 cobram — a SOMA das tres
             * reprovas — sobrevive justamente por isso.
             *
             * O perdedor gastou os dois sorteios a toa. Custo, nao erro: os
             * sorteios sao funcao pura da chave da origem (S04), entao ele
             * recebeu exatamente os mesmos numeros que receberia sozinho.
             */
            acc->stats.delta_failure++;
          }
        }
        else
        {
          acc->stats.excluded_failure++;
        }
      }
      else
      {
        acc->stats.slope_failure++;
      }
    }
    else
    {
      acc->stats.delta_failure++;
    }

    par_cell_leave (off);
    /* ############ FIM DA REGIAO CRITICA ############ */
  }
  else
  {
    acc->stats.z_failure++;
  }


  return val;
}

static
  void   spr_get_neighbor (int i_in,
                    int j_in,
                    int *i_out,
                    int *j_out,
                    rng_ctx_t ctx)
{
  int i;
  int j;
  int k;
  /*
   * O cursor que era `static int last_index` dentro de util_get_next_neighbor
   * (§B12 do mestre, T5 do S04). Zerar aqui nao e load-bearing: a chamada
   * logo abaixo passa um indice explicito, que o ATRIBUI antes de qualquer
   * leitura. O zero existe para o -Wmaybe-uninitialized e para quem le.
   */
  int cursor = 0;

  util_get_next_neighbor (i_in, j_in, i_out, j_out,
                          rng_next_int (ctx, RNG_SUB_NGHBR, 8), &cursor);
  for (k = 0; k < 8; k++)
  {
    i = (*i_out);
    j = (*j_out);
    if (IMAGE_PT (i, j))
    {
      break;
    }
    util_get_next_neighbor (i_in, j_in, i_out, j_out, -1, &cursor);
  }
}

static
    bool
  spr_urbanize_nghbr (int i,
                      int j,
                      int *i_nghbr,
                      int *j_nghbr,
                      SWGHT_TYPE * swght,
                      PIXEL pixel_value,
                      int *stat,
                      rng_ctx_t ctx,
                      par_acc_t * acc)
{

  bool status = FALSE;

  if (IMAGE_PT (i, j))
  {
    /*
     * As duas chamadas compartilham a mesma chave e se distinguem pelo
     * subpasso: NGHBR escolhe o vizinho, SLOPE e EXCLD decidem a urbanizacao.
     * Ver a conferencia de colisao em rng.h.
     */
    spr_get_neighbor (i,                                     /* IN    */
                      j,                                     /* IN    */
                      i_nghbr,                               /* OUT   */
                      j_nghbr,                               /* OUT   */
                      ctx);

    status = spr_urbanize ((*i_nghbr),
                           (*j_nghbr),
                           swght,
                           pixel_value,
                           stat,
                           ctx,
                           acc);
  }
  return status;
}


static
    bool
  spr_road_walk (int i_road_start,
                 int j_road_start,
                 int *i_road_end,
                 int *j_road_end,
                 double diffusion_coefficient,
                 rng_ctx_t ctx)
{
  GRID_P roads = g_roads;

  int i;
  int j;
  int i_nghbr;
  int j_nghbr;
  int k;
  bool end_of_road;
  bool spread = FALSE;
  int run_value;
  int run = 0;
  /* mesmo cursor de spr_get_neighbor; ver a nota la e util.c */
  int cursor = 0;
  /*
   * O UNICO PONTO DE SORTEIO QUE NAO ACEITA UM SUBPASSO CONSTANTE.
   *
   * O §T4 do spec manda dar a cada um dos dez pontos um subpasso "distinto e
   * constante", e para nove deles funciona. Este nao: o sorteio acontece uma
   * vez POR PASSO de uma caminhada de comprimento variavel — `run_value`
   * escala com o coeficiente de difusao e com o valor da estrada, entao o
   * laco abaixo pode dar um passo ou dezenas. Nao existe constante que sirva.
   *
   * A saida e RNG_SUB_WALK ser uma BASE e nao um valor: o passo n usa
   * RNG_SUB_WALK + n. Por isso ela vale 16 e nao 9 — a faixa [16, 65535] fica
   * livre para a caminhada sem encostar em nenhum outro subpasso. Divergencia
   * registrada em §B do spec do estagio.
   */
  int passo = 0;

  i = i_road_start;
  j = j_road_start;
  end_of_road = FALSE;
  while (!end_of_road)
  {
    end_of_road = TRUE;
    util_get_next_neighbor (i, j, &i_nghbr, &j_nghbr,
                            rng_next_int (ctx, RNG_SUB_WALK + passo, 8),
                            &cursor);
    passo++;
    for (k = 0; k < 8; k++)
    {
      if (IMAGE_PT (i_nghbr, j_nghbr))
      {
        if (roads[OFFSET (i_nghbr, j_nghbr)])
        {
          end_of_road = FALSE;
          run++;
          i = i_nghbr;
          j = j_nghbr;
          break;
        }
      }
      util_get_next_neighbor (i, j, &i_nghbr, &j_nghbr, -1, &cursor);
    }
    run_value = (int) (roads[OFFSET (i, j)] / MAX_ROAD_VALUE *
                       diffusion_coefficient);
    if (run > run_value)
    {
      end_of_road = TRUE;
      spread = TRUE;
      (*i_road_end) = i;
      (*j_road_end) = j;
    }
  }

  return spread;
}



static
    bool
  spr_road_search (int i_grwth_center,
                   int j_grwth_center,
                   int *i_road,
                   int *j_road,
                   int max_search_index)
{
  GRID_P roads = g_roads;

  int i;
  int j;
  int i_offset;
  int j_offset;
  bool road_found = FALSE;
  int srch_index;

  for (srch_index = 0; srch_index < max_search_index; srch_index++)
  {
    spr_spiral (srch_index, &i_offset, &j_offset);
    i = i_grwth_center + i_offset;
    j = j_grwth_center + j_offset;

    if (IMAGE_PT (i, j))
    {
      if (roads[OFFSET (i, j)])
      {
        road_found = TRUE;
        (*i_road) = i;
        (*j_road) = j;
        break;
      }
    }
  }


  return road_found;
}



void   spr_spiral (int index,                                     
              int *i_out,                                    
              int *j_out)                                  
{
  bool bn_found;
  int i;
  int j;
  int bn;
  int bo;
  int total;
  int left_side_len;
  int right_side_len;
  int top_len;
  int bot_len;
  int range1;
  int range2;
  int range3;
  int range4;
  int region_offset;
  int nrows;
  int ncols;

  nrows = igrid_GetNumRows ();
  ncols = igrid_GetNumCols ();

  bn_found = FALSE;
  for (bn = 1; bn < MAX (ncols, nrows); bn++)
  {
    total = 8 * ((1 + bn) * bn) / 2;
    if (total > index)
    {
      bn_found = TRUE;
      break;
    }
  }
  if (!bn_found)
  {
    exit (1);
  }
  bo = index - 8 * ((bn - 1) * bn) / 2;
  left_side_len = right_side_len = bn * 2 + 1;
  top_len = bot_len = bn * 2 - 1;
  range1 = left_side_len;
  range2 = left_side_len + bot_len;
  range3 = left_side_len + bot_len + right_side_len;
  range4 = left_side_len + bot_len + right_side_len + top_len;
  if (bo < range1)
  {
    region_offset = bo % range1;
    i = -bn + region_offset;
    j = -bn;
  }
  else if (bo < range2)
  {
    region_offset = (bo - range1) % range2;
    i = bn;
    j = -bn + 1 + region_offset;
  }
  else if (bo < range3)
  {
    region_offset = (bo - range2) % range3;
    i = bn - region_offset;
    j = bn;
  }
  else if (bo < range4)
  {
    region_offset = (bo - range3) % range4;
    i = -bn;
    j = bn - 1 - region_offset;
  }
  else
  {
    exit (1);
  }
  *i_out = i;
  *j_out = j;
}


/*
 * spr_spread — ponto de entrada do nucleo. Simula UM ano.
 *
 * Sequencia: coeficientes -> zera delta -> tabela de pesos de declividade ->
 * fase 1n3 -> fase 4 -> fase 5 -> dois filtros -> merge de delta em z.
 *
 * Sobre o emprestimo de grades: o original pedia duas grades de rascunho ao
 * memory_obj (scratch_gif1 virava delta, scratch_gif3 virava o workspace da
 * fase 5) e tres grades de entrada ao igrid_obj, devolvendo todas no fim.
 * Com as matrizes globais isso sumiu — sao g_delta e g_workspace, e as
 * entradas ja estao em g_slp/g_excld/g_roads. Ver stubs.h.
 *
 * As tres fases LEEM z e escrevem exclusivamente em delta; z so muda no laco
 * de merge, no fim desta funcao. Esse double buffering ja existia no SLEUTH
 * original (SPEC.md §B2, invariante V2) e e o que torna a paralelizacao por
 * faixas de linhas segura na leitura.
 */
/* ------------------------------------------------------------------------ *
 * O CORPO DO ANO, visto por um worker — estagio S05.
 *
 * spr_spread deixou de executar as etapas e passou a DISPARA-LAS: monta o
 * pacote abaixo, chama par_run, e o que cada uma das N threads roda e
 * spr_year_body. Com --threads 1 nao ha thread nenhuma e a sequencia e
 * exatamente a de antes do estagio (ver par_run).
 *
 * A sequencia e a mesma de sempre, agora com um ponto de encontro entre as
 * etapas (V8: nenhuma comeca antes de a anterior terminar em TODAS as
 * threads):
 *
 *   zerar delta -> | -> fase 1n3 -> | -> fase 4 -> | -> fase 5 -> |
 *               -> filtros -> | -> merge -> (join)
 *
 * As etapas marcadas `SERIAL` rodam so no worker 0 enquanto os demais esperam
 * na barreira. Nao e preguica: as fases 1n3 e 5 fazem um numero FIXO de
 * sondagens com dependencia entre iteracoes — a fase 5 caminha pela malha
 * viaria e cada passo depende do anterior — e juntas valem 1 % dos sorteios e
 * ~11 % do relogio (§D1, §D7 do spec do estagio). Paralelizar isso custaria a
 * legibilidade do modelo e nao compraria nada.
 * ------------------------------------------------------------------------ */

typedef struct
{
  SWGHT_TYPE *swght;
  COEFF_TYPE  road_gravity;
  COEFF_TYPE  diffusion_coefficient;
  COEFF_TYPE  breed_coefficient;
  COEFF_TYPE  spread_coefficient;
  int         total_pixels;
  /*
   * Saidas das fases SERIAIS: so o worker 0 as toca, entao continuam sendo
   * ponteiro cru para a variavel do chamador. `og` nao esta aqui — ele vem da
   * fase 4, que e paralela, e por isso vive no par_acc_t (T5).
   */
  int        *sng;
  int        *sdc;
  int        *rt;
} spr_year_t;

static void
  spr_year_body (par_worker_t *w)
{
  spr_year_t *y = (spr_year_t *) w->arg;

  /*
   * O relogio foi armado em spr_spread, ANTES de par_run. O que separa os
   * dois pontos e o pthread_create das N-1 threads, entao esta marca isola o
   * custo de montar o pool do custo de fazer o trabalho. Com uma thread da
   * ~0; com oito, nao (§B22).
   */
  if (w->id == 0) PROBE_MARCA (0);

  /* --- etapa 1: zerar delta ------------------------- O(N), PARALELA ---- */
  /*
   * Escrita disjunta por faixa: o worker so toca [pix_lo, pix_hi). Nao
   * compartilha nada com ninguem e nao precisa de mecanismo nenhum.
   *
   * "Inicializacao" tambem e O(total_pixels) — 5,6 % do relogio no demo200,
   * ~19 % a 1000x1000. Ficou de fora da primeira versao da propria sonda
   * (§D2), e e o exemplo pequeno e verdadeiro de que perfil se mede.
   */
  util_init_grid_range (g_delta, 0, w->pix_lo, w->pix_hi);
  par_barrier (w);
  if (w->id == 0) PROBE_MARCA (1);

  /* --- etapa 2: fase 1n3 ------------------------------------ SERIAL ---- */
  if (w->id == 0)
    spr_phase1n3 (y->diffusion_coefficient, y->breed_coefficient, y->swght,
                  y->sng, y->sdc, &w->acc);
  par_barrier (w);
  if (w->id == 0) PROBE_MARCA (2);

  /* --- etapa 3: fase 4 ------------------------------- O(N), PARALELA --- */
  /*
   * O degrau mais alto da escala: 51,7 % do relogio no demo200 e a UNICA
   * etapa com escrita contestada. A leitura cruza a fronteira da faixa
   * (util_count_neighbors olha os 8 vizinhos) e e segura porque `z` e
   * imutavel durante as fases — V2, §B2 do mestre. A ESCRITA cruza a
   * fronteira em ate uma linha para cada lado (§D3) e por isso precisa de
   * exclusao mutua; e a variante A, dentro de spr_urbanize.
   */
  spr_phase4 (y->spread_coefficient, y->swght, w);
  par_barrier (w);
  if (w->id == 0) PROBE_MARCA (3);

  /* --- etapa 4: fase 5 -------------------------------------- SERIAL ---- */
  if (w->id == 0)
    spr_phase5 (y->road_gravity, y->diffusion_coefficient,
                y->breed_coefficient, y->swght, y->rt, &w->acc);
  par_barrier (w);
  if (w->id == 0) PROBE_MARCA (4);

  /* --- etapa 5: os dois filtros --------------------- O(N), PARALELA ---- */
  /*
   * O caso mais simples do trabalho inteiro: cada pixel e decidido olhando
   * so para aquele pixel. Sem vizinhanca, sem acumulador, sem ordem.
   *
   * NAO HA BARREIRA ENTRE OS DOIS FILTROS, e isso e uma afirmacao e nao um
   * esquecimento: o segundo le `excld` (imutavel) e escreve em `delta` no
   * MESMO indice que o primeiro acabou de escrever. Como os dois so tocam
   * pixels da propria faixa, o unico leitor do que o primeiro escreveu e a
   * propria thread. Uma barreira aqui seria sincronizacao paga por nada.
   */
  util_condition_gif_range (w->pix_lo, w->pix_hi,
                            g_delta, GT, PHASE5G, g_delta, 0);
  util_condition_gif_range (w->pix_lo, w->pix_hi,
                            g_excld, GE, 100,     g_delta, 0);
  par_barrier (w);
  if (w->id == 0) PROBE_MARCA (5);

  /* --- etapa 6: merge ------------------------------- O(N), PARALELA ---- */
  /*
   * O UNICO ponto do ano em que z e escrito (invariante V2) — e, ainda
   * assim, disjunto por faixa: o merge le e escreve o MESMO indice, entao
   * nenhuma thread encosta no pixel de outra. A grade nao e o problema aqui.
   *
   * O que este degrau acrescenta em relacao aos filtros sao os ACUMULADORES:
   * num_growth_pix e a soma das declividades atravessam a faixa. Cada worker
   * soma no proprio par_acc_t e a reducao acontece depois do join (V5). Nao
   * ha trava — ninguem le o acumulador do vizinho enquanto o ano corre.
   *
   * `slope_sum` e long por causa de §D5: e a decisao que impede o
   * average_slope de mudar com o numero de threads. Ver par.h.
   *
   * E A CONTAGEM DE POPULACAO MORA AQUI DESDE O S06 (T1, §D6 do spec).
   * Era a sexta varredura O(N) da funcao, serial, depois do join. Nao foi
   * paralelizada: foi ELIMINADA, porque este laco ja passa por todo pixel e ja
   * escreve o valor final de z[i]. Ver par.h, no comentario de par_acc_t.pop,
   * para por que a soma das contagens por faixa e exatamente a contagem
   * global.
   */
  {
    /*
     * Uma declaracao por linha: GRID_P e `PIXEL *` por macro, entao
     * `GRID_P a = ..., b = ...` declara b como `long`, nao `long *`.
     */
    GRID_P z     = g_z;
    GRID_P delta = g_delta;
    GRID_P slp   = g_slp;
    int i;
    /*
     * Acumulador na PILHA, e nao `w->acc.pop++` como os outros dois. A
     * diferenca nao e estilo: growth_pix e slope_sum so sao tocados no ramo
     * raro (pixel que cresceu), e este e tocado em TODO pixel. Em memoria, no
     * laco mais quente do merge, seria o contrario do que §D6 promete.
     */
    int pop = 0;

    for (i = w->pix_lo; i < w->pix_hi; i++)
    {
      if ((z[i] == 0) && (delta[i] > 0))
      {
        /* new growth being placed into array */
        w->acc.slope_sum += (long) slp[i];
        z[i] = delta[i];
        w->acc.growth_pix++;
      }
      /*
       * Depois da escrita, de proposito: a celula que acabou de crescer conta
       * no ano em que cresceu, que e o que util_count_pixels fazia rodando
       * depois do merge inteiro.
       */
      if (z[i] >= PHASE0G)
        pop++;
    }

    w->acc.pop += pop;
  }
  /* sem barreira: o join de par_run e a ultima, e a mais forte que ha */
}


void   spr_spread (
               float *average_slope,
               int *num_growth_pix,
               int *sng,
               int *sdc,
               int *og,
               int *rt,
               int *pop
  )
{
  spr_year_t y;
  par_acc_t  total;
  SWGHT_TYPE swght[SLOPE_WEIGHT_ARRAY_SZ];

  y.road_gravity          = coeff_GetCurrentRoadGravity ();
  y.diffusion_coefficient = coeff_GetCurrentDiffusion ();
  y.breed_coefficient     = coeff_GetCurrentBreed ();
  y.spread_coefficient    = coeff_GetCurrentSpread ();
  y.total_pixels          = mem_GetTotalPixels ();
  y.sng = sng;
  y.sdc = sdc;
  y.rt  = rt;
  y.swght = swght;

  /*
   * A tabela de pesos de declividade e O(256), nao O(total_pixels): fica fora
   * da regiao paralela de proposito, calculada uma vez pela thread que
   * dispara. Subiu para antes de zerar delta — as duas sao independentes, e
   * assim o corpo do ano so contem etapa que vale a pena repartir.
   */
  spr_get_slp_weights (SLOPE_WEIGHT_ARRAY_SZ, swght);

  PROBE_INICIO ();
  PROBE_ANO ();

  par_run (spr_year_body, &y);
  par_reduce (&total);

  PROBE_MARCA (6);

  /*
   * A REDUCAO (V5), em tres linhas e sem trava nenhuma.
   *
   * Os N acumuladores foram somados em ordem fixa por par_reduce, ja depois
   * do join. `og` e o unico contador de crescimento que passa por aqui —
   * sng, sdc e rt vieram das fases seriais e ja estao escritos.
   */
  (*og) = total.og;
  stats_Accumulate (&total.stats);

  (*num_growth_pix) = total.growth_pix;

  /*
   * §D5 do spec: a soma veio em `long`, exata e independente de N, e a
   * conversao para float acontece UMA VEZ, aqui. O original acumulava em
   * float dentro do laco; numa grade grande as duas formas divergem, e e a
   * nossa que esta certa. Seguro para V7 por causa de §B16 do mestre — este
   * average_slope e saida morta no SLEUTH original, e a coluna `slope` do
   * avg.log vem de outro lugar (urban_mean_slope, em main.c).
   */
  (*average_slope) = (float) total.slope_sum;

  /*
   * A VARREDURA O(N) QUE DEIXOU DE EXISTIR — estagio S06, tarefa T1.
   *
   * Aqui havia
   *
   *     *pop = util_count_pixels (y.total_pixels, g_z, GE, PHASE0G);
   *
   * a sexta varredura da funcao (§B24 do mestre), serial e depois do join:
   * 12,53 ms e 12,4 % do ano a 2000x2000 com 8 threads, identicos com 1 e com
   * 8 threads porque nenhuma thread a tocava.
   *
   * Nao foi paralelizada. Foi fundida no laco do merge, que ja percorre a
   * grade inteira (§D6 do spec, e o comentario de par_acc_t.pop em par.h). O
   * que sobra aqui e a leitura do acumulador ja reduzido — O(nthreads), nao
   * O(total_pixels).
   *
   * A sonda perdeu o slot correspondente junto, e isso e afirmacao e nao
   * esquecimento: nao ha mais passada separada para medir. O efeito aparece
   * como o "total" encolhendo e o "merge" crescendo um pouco.
   */
  *pop = total.pop;

  if (*num_growth_pix == 0)
  {
    *average_slope = 0.0;
  }
  else
  {
    *average_slope /= (float) *num_growth_pix;
  }

  /*
   * O original devolvia aqui as cinco grades emprestadas (igrid_GridRelease
   * e mem_GetWGridFree). Com as matrizes globais nao ha emprestimo — a
   * liberacao e uma vez so, em grid_free(), no fim do programa.
   */
}
