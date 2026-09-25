/*
 * par.h — o particionamento, o pool e as barreiras. Estagio S05.
 *
 * Este e o arquivo em que o trabalho deixa de ser preparacao e passa a ser
 * paralelismo. O S04 removeu o que impedia as threads de serem
 * deterministicas (o gerador virou funcao pura da chave, o cursor de vizinhos
 * virou variavel de pilha); aqui elas aparecem.
 *
 * TRES CONCEITOS, E SO TRES:
 *
 *   1. UMA faixa de linhas por worker, estatica (SPEC.md §C). A mesma faixa
 *      serve as quatro varreduras do estagio — as tres que sao por pixel
 *      (zerar delta, filtros, merge) a leem como intervalo linear pix_lo/
 *      pix_hi, e a fase 4 a le como intervalo de linhas row_lo/row_hi porque
 *      precisa da vizinhanca. Sao a MESMA divisao, expressa em duas unidades,
 *      e nao duas divisoes: pix_lo e literalmente row_lo * g_ncols.
 *
 *   2. UMA barreira, reusada entre as etapas (V8). As fases sao sequenciais
 *      entre si; nenhuma comeca antes de a anterior terminar em todas as
 *      threads.
 *
 *   3. UM acumulador por worker, reduzido no fim (V5). Nenhum contador do
 *      caminho paralelo e incrementado sem protecao — a protecao aqui nao e
 *      trava, e propriedade exclusiva: cada worker so toca o proprio.
 *
 * POR QUE A LEITURA CRUZA A FRONTEIRA SEM SINCRONIZACAO. A fase 4 le `z` dos
 * oito vizinhos, e nas linhas de borda da faixa isso cai na faixa vizinha.
 * E seguro porque `z` e imutavel durante as fases (V2, §B2 do mestre): quem
 * escreve durante as fases e `delta`, e `z` so muda no merge, depois de uma
 * barreira. E esse achado — que o double buffering ja existia no SLEUTH
 * original — que torna o particionamento por faixas viavel.
 *
 * O QUE *NAO* E SEGURO e a ESCRITA em `delta` na fronteira: uma celula de
 * origem em (row,col) urbaniza um dos 8 vizinhos, entao escreve em row-1, row
 * ou row+1. Uma faixa [a,b] escreve em [a-1, b+1], e as duas linhas de
 * fronteira sao contestaveis. Dai a variante A (§D6 do spec do estagio).
 */
#ifndef PAR_H
#define PAR_H

#include "globals.h"

/* ------------------------------------------------------------------------ *
 * Configuracao — escrita uma vez, antes do primeiro ano; lida por todas as
 * threads durante a simulacao. Mesma garantia de imutabilidade de
 * g_nrows/g_ncols (globals.h) e de g_rng_mode (rng.h), e pelo mesmo motivo:
 * e o que permite le-las sem sincronizacao.
 * ------------------------------------------------------------------------ */

#define PAR_VARIANT_NONE  0   /* sem exclusao — modo de DEMONSTRACAO (§B)   */
#define PAR_VARIANT_A     1   /* mutex global em torno do RMW sobre delta   */
#define PAR_VARIANT_B     2   /* vetor de mutexes indexado pela celula alvo */
#define PAR_VARIANT_C     3   /* CAS atomico: nao ha regiao critica (§D4)   */

/*
 * QUANTAS TRAVAS TEM A VARIANTE B, e as duas contas que escolheram o numero.
 *
 * Potencia de dois para que o indice seja `off & (PAR_SHARDS - 1)` — uma
 * mascara, nao uma divisao, no caminho mais quente do programa.
 *
 * 1024 travas cobrem com folga o que ha para cobrir. A trava e tomada ~10 mil
 * vezes por ano na grade ladrilhada (§D3: 52 163 em 5 anos), espalhadas por 4
 * milhoes de celulas, e o que se quer e que duas threads simultaneas caiam em
 * travas diferentes. Com 1024 baldes a colisao e ~0,1 % — contra 100 % da
 * variante A, que e o desperdicio que o estagio mede.
 *
 * E o vetor e de structs alinhadas a 64 bytes, nao de pthread_mutex_t crus.
 * §B4 do mestre: a linha de cache tem 64 bytes e o pthread_mutex_t da glibc
 * tem 40, entao um vetor cru poria travas vizinhas na MESMA linha — e duas
 * threads trancando celulas diferentes ficariam disputando a linha de cache,
 * que e o falso compartilhamento reaparecendo justamente no mecanismo criado
 * para evita-lo. 1024 x 64 B = 64 KB, estaticos, fora do laco de fases (V9).
 *
 * Sobrescritivel na compilacao (-DPAR_SHARDS=N) para que a sonda do T5 possa
 * varrer o numero em vez de o texto ter de afirmar que 1024 e o certo.
 */
#ifndef PAR_SHARDS
#define PAR_SHARDS  1024
#endif

extern int g_threads;
extern int g_variant;

const char *par_variant_name (void);

/*
 * Traduz o texto de --variant no codigo PAR_VARIANT_*, ou devolve -1 se o
 * nome nao existe. As duas direcoes moram aqui, e nao em main.c, porque a
 * lista de variantes e desta camada: acrescentar uma variante sem ensinar a
 * linha de comando a aceita-la deixaria de compilar em um lugar so.
 */
int par_variant_code (const char *name);

/* ------------------------------------------------------------------------ *
 * Acumuladores por thread (V5)
 *
 * Cada worker tem o seu. Ninguem le o do outro enquanto o ano corre; a soma
 * acontece depois do join, que e a ultima barreira do ano.
 *
 * `slope_sum` e LONG e nao FLOAT, e essa e a decisao mais sutil do estagio
 * (§D5 do spec). O merge original faz `average_slope += (float) slp[i]`. Soma
 * em ponto flutuante nao e associativa: repartir a soma em 4 parciais e somar
 * as parciais da resultado diferente de repartir em 2, entao uma reducao
 * ingenua produziria um average_slope que MUDA COM O NUMERO DE THREADS — e V3
 * exige igualdade byte a byte para todo N. Seria uma quebra de V3 legitima e
 * chata: nada errado com o paralelismo, so aritmetica.
 *
 * A saida e que os valores somados sao INTEIROS — slp e GRID_P, isto e,
 * long *. Acumular em long e converter uma unica vez no fim e exato,
 * independente da ordem e independente de N. O conserto do primeiro risco a
 * V3 deste estagio nao e uma trava: e perceber que os valores eram inteiros o
 * tempo todo.
 * ------------------------------------------------------------------------ */

typedef struct
{
  int  og;               /* fase 4: urbanizacoes organicas                  */
  int  growth_pix;       /* merge:  num_growth_pix                          */
  long slope_sum;        /* merge:  soma das declividades, EXATA (§D5)      */
  int  pop;              /* merge:  populacao urbana (S06 T1, §D6)         */
  stats_counters_t stats;  /* os cinco contadores de spr_urbanize           */
} par_acc_t;

/*
 * SOBRE `pop`, E POR QUE ELE NAO E UMA VARREDURA PARALELIZADA — estagio S06,
 * tarefa T1.
 *
 * A contagem de populacao era a sexta varredura O(total_pixels) de spr_spread
 * (§B24 do mestre): `util_count_pixels (total_pixels, g_z, GE, PHASE0G)`, no
 * fim da funcao, 12,53 ms e 12,4 % do ano a 2000x2000 com 8 threads — e
 * SERIAL, entao custava o mesmo com 1 e com 8 threads.
 *
 * O caminho obvio era reparti-la como o merge: faixa, acumulador, reducao. O
 * caminho escolhido foi outro, e e o ponto do §D6 do spec: o merge JA percorre
 * a grade inteira, JA tem z[i] em registrador e JA escreve o valor final de
 * z[i]. Contar ali custa uma comparacao por pixel e elimina uma passada
 * inteira de leitura da grade.
 *
 * Por que a conta fecha: as faixas particionam [0, total_pixels) sem buraco e
 * sem sobreposicao (ver partition() em par.c, e g_total_pixels = nrows*ncols
 * em grid.c), entao a soma das contagens por faixa E a contagem global. Nao
 * precisa de barreira nova — cada worker le so a propria faixa, que ele mesmo
 * acabou de escrever, que e o mesmo argumento que dispensa a barreira entre os
 * dois filtros.
 *
 * E §B22 explica por que isto ganha mais do que paralelizar ganharia: as
 * varreduras sao memory-bound, entao a passada que mais compensa e a que NAO
 * ACONTECE.
 */

/* ------------------------------------------------------------------------ *
 * O worker
 *
 * `arg` e o contexto que o chamador passou a par_run — em spr_spread, o
 * pacote de ponteiros que o corpo do ano precisa. A faixa vem pronta; o corpo
 * nao calcula divisao nenhuma.
 * ------------------------------------------------------------------------ */

typedef struct par_worker
{
  int id;                /* 0 .. nthreads-1; o 0 e a thread que chamou      */
  int nthreads;
  int row_lo, row_hi;    /* faixa de linhas,  semiaberta: [row_lo, row_hi)  */
  int pix_lo, pix_hi;    /* a mesma faixa em indice linear                  */
  par_acc_t acc;
  void *arg;
} par_worker_t;

/*
 * par_init e chamada UMA VEZ, depois de grid_alloc (precisa de g_nrows) e
 * antes do primeiro ano. E ela que aloca o vetor de workers e monta a
 * barreira — invariante V9: nenhuma alocacao dentro do laco de fases.
 */
void par_init (int nthreads, int variant);
void par_finish (void);

/*
 * par_run dispara o corpo em nthreads workers e so volta quando todos
 * terminaram. Os acumuladores sao zerados na entrada.
 *
 * A thread que chama E o worker 0 — sao criadas nthreads-1 threads, nao
 * nthreads. Com --threads 1 nao se cria thread nenhuma e nao se toca em
 * barreira nenhuma: o caminho e o serial de antes do estagio mais a conta da
 * faixa, que e o que permite a VS21 comparar bit a bit contra a saida
 * pre-S05. Pequeno desvio do §T1 do spec ("uma thread por faixa"), registrado
 * em §B do estagio.
 */
void par_run (void (*body) (par_worker_t *), void *arg);

/* ponto de encontro entre etapas (V8). No-op quando nthreads == 1. */
void par_barrier (par_worker_t *w);

/* soma os acumuladores dos nthreads workers. Chamar depois de par_run. */
void par_reduce (par_acc_t *out);

/* ------------------------------------------------------------------------ *
 * A REGIAO CRITICA (V6) — e a interface e da OPERACAO, nao do mecanismo
 *
 * Estagio S06, tarefa T2. Aqui havia `par_lock()` / `par_unlock()`, que
 * exprimiam "entrar numa regiao critica" e serviam a variante A. Elas nao
 * comportam as tres variantes, e o motivo e conceitual e nao de estilo
 * (§D4 do spec do estagio):
 *
 *   A (mutex global) e B (sharding) sao a MESMA ideia com granularidade
 *   diferente — uma trava contra um vetor de travas indexado pela celula. Para
 *   elas, "entrar e sair" descreve bem o que acontece.
 *
 *   C (CAS) nao e uma regiao critica. Nao ha entrada nem saida: ha uma
 *   operacao atomica que ou toma a celula, ou descobre que alguem ja a tomou.
 *   Nao existe `par_lock` que exprima isso — forcar C na interface de trava
 *   daria um lock() que nao tranca nada e um CAS solto no meio de
 *   spr_urbanize, com o leitor tendo de adivinhar quais linhas estao
 *   protegidas em qual variante.
 *
 * A interface honesta e, portanto, a das duas coisas que o codigo realmente
 * faz com a celula: LER para decidir a cascata, e TENTAR GRAVAR.
 *
 * O preco disto e um ramo novo em spr_urbanize — o commit que falha —, MORTO
 * sob A e sob B, porque la a trava ja garantiu a exclusao antes da leitura.
 * Sob C ele e o caminho normal da disputa perdida. Um ramo morto em duas das
 * tres variantes e mais honesto que um lock() que nao tranca.
 * ------------------------------------------------------------------------ */

/*
 * Le delta[off] para decidir a cascata.
 *   A, B: leitura simples — ja se esta sob a trava, ninguem mais escreve.
 *   C:    carga atomica relaxed, e e so uma DICA: entre esta leitura e o
 *         commit, outra thread pode tomar a celula. Quem decide e o commit.
 */
PIXEL par_cell_read (GRID_P delta, int off);

/*
 * Tenta gravar `value` em delta[off], que o chamador leu como 0.
 *   A, B: grava e devolve TRUE — a trava garante que ninguem entrou no meio.
 *   C:    compare-and-swap de 0 para value. FALSE quer dizer "outra thread
 *         chegou primeiro", e o chamador conta delta_failure (§D5).
 *
 * Opera sobre PIXEL, que e `long` de 8 bytes (§B4 do mestre). O trecho da
 * variante C no plano original usa `unsigned char expected = 0` e esta
 * ERRADO: um CAS na largura errada nao protege a celula, e o ThreadSanitizer
 * acusa (VS26).
 */
bool par_cell_commit (GRID_P delta, int off, PIXEL value);

/*
 * Delimitam a exclusao, quando ha alguma a delimitar.
 *   A: mutex global.  B: mutex[off % PAR_SHARDS].  C: no-op.
 *
 * Com uma thread as tres viram no-op: nao se paga exclusao para nao competir
 * com ninguem, e e isso que permite a VS21 comparar bit a bit contra a saida
 * pre-S05.
 */
void par_cell_enter (int off);
void par_cell_leave (int off);

#endif /* PAR_H */
