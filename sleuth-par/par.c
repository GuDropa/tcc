/*
 * par.c — implementacao do particionamento, do pool e das barreiras.
 *
 * Ver par.h para o desenho e para o porque de cada escolha. Aqui so ha
 * mecanica: dividir linhas, criar threads, esperar, somar.
 *
 * O arquivo inteiro e deliberadamente burro. Toda a sutileza do estagio mora
 * em spread.c (o que cada etapa pode fazer em paralelo) e em rng.c (por que o
 * sorteio nao depende de quem executa). Se este arquivo precisar ficar
 * esperto, provavelmente o particionamento esta errado.
 */

/*
 * O Makefile compila com -std=c11, o que define __STRICT_ANSI__ e faz a
 * glibc esconder tudo o que nao e C puro — inclusive pthread_barrier_t, que e
 * POSIX 2001 e nao C11. Sem esta linha o erro e `unknown type name
 * 'pthread_barrier_t'`, que soa como pthread faltando e nao e.
 *
 * Fica aqui, e nao no CFLAGS, para valer so onde e necessario: quem abrir
 * este arquivo ve o motivo junto com o sintoma.
 */
#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "globals.h"
#include "par.h"

int g_threads = 1;
int g_variant = PAR_VARIANT_NONE;

/*
 * Uma trava por linha de cache. Ver o comentario de PAR_SHARDS em par.h: sem
 * o _Alignas, travas vizinhas do vetor cairiam na mesma linha de 64 bytes e o
 * sharding recriaria, em falso compartilhamento, a disputa que ele existe
 * para eliminar (§B4 do mestre).
 */
typedef struct
{
  _Alignas (64) pthread_mutex_t m;
} par_shard_t;

static par_shard_t shard[PAR_SHARDS];

/*
 * O indice do shard e `off & (PAR_SHARDS - 1)`, uma mascara, e mascara so
 * equivale a resto quando o divisor e potencia de dois. Com PAR_SHARDS = 1000
 * o codigo continuaria compilando e rodando CERTO — a mascara daria um indice
 * valido —, mas usaria 512 dos 1000 baldes e ninguem perceberia; seria o
 * mecanismo de B medindo metade do que diz medir. Como PAR_SHARDS e
 * sobrescritivel na compilacao para a sonda do T5 varre-lo, a guarda importa.
 */
_Static_assert ((PAR_SHARDS & (PAR_SHARDS - 1)) == 0,
                "PAR_SHARDS tem de ser potencia de dois - ver par.h");

static struct
{
  int               nthreads;
  par_worker_t     *w;
  pthread_t        *tid;         /* [0] nao e usado: o worker 0 e quem chama */
  pthread_barrier_t barrier;
  int               barrier_ready;
  pthread_mutex_t   delta_lock;
  /*
   * A variante EFETIVA, que nao e a pedida na linha de comando: com uma
   * thread ela e sempre NONE, porque nao se paga exclusao para nao competir
   * com ninguem. E o que faz o caminho de --threads 1 ser byte a byte o
   * serial de antes do S05 em qualquer variante (VS21, VS24).
   */
  int               excl;
  int               shards_ready;
  void            (*body) (par_worker_t *);
} pool;

static void
  fatal (const char *msg)
{
  fprintf (stderr, "par: %s\n", msg);
  exit (EXIT_FAILURE);
}

int
  par_variant_code (const char *name)
{
  if (strcmp (name, "A")    == 0) return PAR_VARIANT_A;
  if (strcmp (name, "B")    == 0) return PAR_VARIANT_B;
  if (strcmp (name, "C")    == 0) return PAR_VARIANT_C;
  if (strcmp (name, "none") == 0) return PAR_VARIANT_NONE;
  return -1;
}

const char *
  par_variant_name (void)
{
  switch (g_variant)
  {
  case PAR_VARIANT_A: return "A";
  case PAR_VARIANT_B: return "B";
  case PAR_VARIANT_C: return "C";
  default:            return "none";
  }
}

/* ------------------------------------------------------------------------ *
 * Particionamento estatico por faixas de linhas
 *
 * A conta e a classica de resto distribuido: a faixa do worker i vai de
 * (i * nrows) / n a ((i+1) * nrows) / n. As faixas nunca diferem em mais de
 * uma linha, cobrem [0, nrows) sem buraco e sem sobreposicao, e nao dependem
 * de nada alem de i — o que importa, porque V3 exige que a divisao seja a
 * mesma em toda execucao com o mesmo N.
 *
 * Com nrows < nthreads algumas faixas saem vazias (row_lo == row_hi). Nao e
 * erro: os lacos nao executam e o worker so participa das barreiras. E o que
 * acontece numa grade 20x20 com --threads 16, 32 ou 64 — VS31 exercita os
 * tres e exige o z do serial.
 *
 * Ate o S07 esta linha dizia "--threads 32, que os testes exercitam", e
 * nenhum teste passava de 8: era cobertura afirmada e nao conferida, no
 * arquivo que par.h manda ler antes de mexer em thread. A frase so voltou a
 * afirmar alguma coisa depois que VS31 passou a existir. Ver §B1 de
 * specs/S07-rede-de-verificacao.md.
 * ------------------------------------------------------------------------ */

static void
  partition (void)
{
  int i;

  for (i = 0; i < pool.nthreads; i++)
  {
    par_worker_t *w = &pool.w[i];

    w->id       = i;
    w->nthreads = pool.nthreads;
    w->row_lo   = (int) (((long) i       * g_nrows) / pool.nthreads);
    w->row_hi   = (int) (((long) (i + 1) * g_nrows) / pool.nthreads);
    w->pix_lo   = w->row_lo * g_ncols;
    w->pix_hi   = w->row_hi * g_ncols;
  }
}

void
  par_init (int nthreads, int variant)
{
  if (nthreads < 1)
    fatal ("--threads tem de ser >= 1");
  if (g_nrows <= 0)
    fatal ("par_init chamada antes de grid_alloc");

  g_threads = nthreads;
  g_variant = variant;

  pool.nthreads = nthreads;
  pool.w   = calloc ((size_t) nthreads, sizeof (par_worker_t));
  pool.tid = calloc ((size_t) nthreads, sizeof (pthread_t));
  if (pool.w == NULL || pool.tid == NULL)
    fatal ("sem memoria para o vetor de workers");

  partition ();

  if (nthreads > 1)
  {
    if (pthread_barrier_init (&pool.barrier, NULL, (unsigned) nthreads) != 0)
      fatal ("pthread_barrier_init falhou");
    pool.barrier_ready = 1;
  }

  /*
   * A exclusao so entra no caminho quando ha com quem competir. Com uma
   * thread, travar seria pagar por um problema que nao existe — e mediria
   * errado o custo da variante no S08.
   */
  pool.excl = (nthreads > 1) ? variant : PAR_VARIANT_NONE;

  if (pthread_mutex_init (&pool.delta_lock, NULL) != 0)
    fatal ("pthread_mutex_init falhou");

  /*
   * As 1024 travas da variante B nascem aqui, UMA vez, antes do primeiro ano
   * (V9). Sao ~64 KB de BSS e alguns microssegundos de inicializacao; o custo
   * que o T5 vai medir e o de TOMAR a trava, nao o de cria-la, e por isso
   * esta linha fica longe do laco.
   */
  if (variant == PAR_VARIANT_B)
  {
    int s;

    for (s = 0; s < PAR_SHARDS; s++)
      if (pthread_mutex_init (&shard[s].m, NULL) != 0)
        fatal ("pthread_mutex_init do vetor de shards falhou");
    pool.shards_ready = 1;
  }
}

void
  par_finish (void)
{
  if (pool.barrier_ready)
  {
    pthread_barrier_destroy (&pool.barrier);
    pool.barrier_ready = 0;
  }
  pthread_mutex_destroy (&pool.delta_lock);
  if (pool.shards_ready)
  {
    int s;

    for (s = 0; s < PAR_SHARDS; s++)
      pthread_mutex_destroy (&shard[s].m);
    pool.shards_ready = 0;
  }
  free (pool.w);   pool.w   = NULL;
  free (pool.tid); pool.tid = NULL;
  pool.nthreads = 0;
}

/* ------------------------------------------------------------------------ *
 * Disparo e espera
 * ------------------------------------------------------------------------ */

static void *
  trampoline (void *arg)
{
  par_worker_t *w = (par_worker_t *) arg;

  pool.body (w);
  return NULL;
}

void
  par_run (void (*body) (par_worker_t *), void *arg)
{
  int i;

  for (i = 0; i < pool.nthreads; i++)
  {
    memset (&pool.w[i].acc, 0, sizeof (par_acc_t));
    pool.w[i].arg = arg;
  }
  pool.body = body;

  if (pool.nthreads == 1)
  {
    /*
     * Caminho de uma thread: nenhum pthread_create, nenhuma barreira,
     * nenhuma trava. E o serial de antes do estagio com a faixa ja calculada
     * — e e por isso que VS21 pode exigir igualdade BIT A BIT contra a saida
     * pre-S05 em vez de "equivalencia".
     */
    body (&pool.w[0]);
    return;
  }

  for (i = 1; i < pool.nthreads; i++)
  {
    if (pthread_create (&pool.tid[i], NULL, trampoline, &pool.w[i]) != 0)
    {
      /*
       * Falhar aqui e fatal e nao recuperavel: a barreira foi montada para
       * nthreads participantes, entao seguir com menos threads trava o
       * programa na primeira etapa em vez de dar resultado errado. Melhor
       * morrer dizendo o motivo.
       */
      fatal ("pthread_create falhou");
    }
  }

  body (&pool.w[0]);

  for (i = 1; i < pool.nthreads; i++)
    pthread_join (pool.tid[i], NULL);
}

void
  par_barrier (par_worker_t *w)
{
  if (w->nthreads > 1)
    pthread_barrier_wait (&pool.barrier);
}

/* ------------------------------------------------------------------------ *
 * Reducao (V5)
 *
 * Roda DEPOIS de par_run, isto e, depois do join — que e a ultima barreira do
 * ano e a mais forte que ha. Nada dentro do ano consome os valores reduzidos:
 * num_growth_pix e average_slope so sao usados na divisao final, ja fora da
 * regiao paralela.
 *
 * Somar `long` em ordem fixa e exato (§D5): esta funcao da o mesmo resultado
 * para qualquer N, e e isso que VS20 verifica.
 * ------------------------------------------------------------------------ */

void
  par_reduce (par_acc_t *out)
{
  int i;

  memset (out, 0, sizeof (*out));
  for (i = 0; i < pool.nthreads; i++)
  {
    const par_acc_t *a = &pool.w[i].acc;

    out->og         += a->og;
    out->growth_pix += a->growth_pix;
    out->slope_sum  += a->slope_sum;
    out->pop        += a->pop;

    out->stats.urban_success    += a->stats.urban_success;
    out->stats.z_failure        += a->stats.z_failure;
    out->stats.delta_failure    += a->stats.delta_failure;
    out->stats.slope_failure    += a->stats.slope_failure;
    out->stats.excluded_failure += a->stats.excluded_failure;
  }
}

/* ------------------------------------------------------------------------ *
 * A REGIAO CRITICA — as tres variantes atras da mesma interface (V6)
 *
 * Ver par.h para por que a interface e a da OPERACAO (ler a celula, tentar
 * grava-la) e nao a do MECANISMO (entrar e sair de uma regiao critica): a
 * variante C nao tem regiao critica nenhuma para se entrar.
 *
 * `pool.excl` e a variante EFETIVA: a pedida na linha de comando, ou NONE se
 * ha uma thread so. Com uma thread as quatro funcoes abaixo degeneram no
 * codigo serial de sempre, que e o que VS21 e VS24 exploram.
 * ------------------------------------------------------------------------ */

PIXEL
  par_cell_read (GRID_P delta, int off)
{
  if (pool.excl == PAR_VARIANT_C)
  {
    /*
     * RELAXED, e de proposito. Esta leitura nao decide nada: ela so evita
     * gastar dois sorteios e uma cascata de testes numa celula que ja se ve
     * tomada. Entre ela e o commit, outra thread pode tomar a celula, e e o
     * commit que descobre isso — nao ha ordenacao a garantir aqui porque nao
     * ha dado publicado junto.
     *
     * Ela precisa ser atomica ainda assim: ler `delta[off]` com uma leitura
     * comum enquanto outra thread grava com CAS e corrida de dados pela
     * definicao do C11, e o ThreadSanitizer acusa (VS26) — com razao.
     */
    return __atomic_load_n (&delta[off], __ATOMIC_RELAXED);
  }
  return delta[off];
}

bool
  par_cell_commit (GRID_P delta, int off, PIXEL value)
{
  if (pool.excl == PAR_VARIANT_C)
  {
    /*
     * ############ A VARIANTE C, E ELA CABE EM UMA LINHA ############
     *
     * `expected` e PIXEL, isto e, `long` de 8 bytes — §B4 do mestre. O plano
     * original escreve `unsigned char expected = 0` e esta errado: um CAS de
     * um byte sobre uma celula de oito compara e troca o byte menos
     * significativo e deixa os outros sete desprotegidos. Seria uma variante
     * que parece funcionar e nao exclui nada.
     *
     * Falha nao e erro: FALSE quer dizer que outra thread tomou a celula
     * entre a leitura e agora. O chamador conta delta_failure e segue (§D5).
     *
     * ACQ_REL no sucesso porque o vencedor publica `value` em delta[off], e
     * quem o ler depois — o merge, depois da barreira — tem de ver a escrita
     * inteira. RELAXED na falha porque o perdedor nao publica nada e nao
     * consome nada: ele so vai contar a derrota no proprio acumulador.
     *
     * `weak = false`: a forma forte nao falha por acidente, e falha
     * espuria aqui seria contada como delta_failure. Isso nao quebraria V3 —
     * a saida do modelo nao mudaria —, mas mexeria na reparticao dos
     * contadores por um motivo que nao e o do modelo, e §B21 ja e sutil o
     * bastante sem isso.
     */
    PIXEL expected = 0;

    return __atomic_compare_exchange_n (&delta[off], &expected, value,
                                        false,
                                        __ATOMIC_ACQ_REL,
                                        __ATOMIC_RELAXED) ? TRUE : FALSE;
  }

  /*
   * Sob A e sob B a trava ja garantiu que ninguem escreveu entre a leitura e
   * aqui, entao gravar nao pode falhar. O TRUE nao e otimismo: e a afirmacao
   * de que a exclusao foi feita antes.
   */
  delta[off] = value;
  return TRUE;
}

void
  par_cell_enter (int off)
{
  switch (pool.excl)
  {
  case PAR_VARIANT_A:
    pthread_mutex_lock (&pool.delta_lock);
    break;

  case PAR_VARIANT_B:
    /*
     * A MESMA IDEIA DE A, COM OUTRA GRANULARIDADE — e e so isto que B e.
     *
     * A trava e escolhida pela CELULA ALVO, nao pela thread nem pela faixa.
     * Tem de ser assim: o que precisa ser exclusivo e o read-modify-write
     * sobre delta[off], entao duas threads que mexam em celulas diferentes
     * podem seguir juntas, e duas que mexam na MESMA celula pegam
     * necessariamente a mesma trava. Que duas celulas diferentes caiam no
     * mesmo balde e desperdicio, nao erro — e com 1024 baldes e raro.
     *
     * A mascara depende de PAR_SHARDS ser potencia de dois; o _Static_assert
     * que garante isso esta junto da declaracao do vetor.
     */
    pthread_mutex_lock (&shard[off & (PAR_SHARDS - 1)].m);
    break;

  default:
    /* C nao tem regiao critica para se entrar; NONE nao tem exclusao */
    break;
  }
}

void
  par_cell_leave (int off)
{
  switch (pool.excl)
  {
  case PAR_VARIANT_A:
    pthread_mutex_unlock (&pool.delta_lock);
    break;

  case PAR_VARIANT_B:
    pthread_mutex_unlock (&shard[off & (PAR_SHARDS - 1)].m);
    break;

  default:
    break;
  }
}
