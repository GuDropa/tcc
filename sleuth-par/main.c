/*
 * main.c — orquestracao: monta as matrizes, roda N anos, relata.
 *
 * Conforme o Esqueleto TCC, secao 3.2:
 *
 *     chamar spread() com matrizes de dados iniciais
 *     usar a saida de spread como nova entrada, e calcular novamente
 *     rodar N anos
 *
 * A realimentacao e automatica: o merge no fim de spr_spread ja escreve em
 * g_z, entao o ano seguinte le o resultado do anterior. Nao ha troca de
 * ponteiros aqui.
 *
 * ESTAGIO S05: --threads deixou de ser ignorado. Quem reparte o ano e
 * par_run, disparada de dentro de spr_spread; aqui so se le o numero, se
 * monta o pool uma vez (par_init) e se publica a contagem no cabecalho.
 * --variant continua aceito e ignorado, com aviso, ate o S06.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "globals.h"
#include "grid.h"
#include "load.h"
#include "par.h"
#include "spread.h"

/* ------------------------------------------------------------------------ *
 * Parametros
 * ------------------------------------------------------------------------ */

typedef struct
{
  int  rows;
  int  cols;
  int  dims_given;          /* --rows/--cols vieram da linha de comando?     */
  int  years;
  long seed;
  int  threads;
  const char *variant;
  const char *rng_mode;
  const char *scan_order;
  const char *dump_path;
  const char *load_dir;
  int  want_hash;
  double diffusion;
  double breed;
  double spread;
  double slope_resist;
  double road_gravity;
  double critical_slope;
} options_t;

static void
  usage (const char *argv0)
{
  fprintf (stderr,
    "uso: %s [opcoes]\n"
    "\n"
    "  --rows R        linhas da grade      (padrao 20)\n"
    "  --cols C        colunas da grade     (padrao 20)\n"
    "  --years Y       anos a simular       (padrao 5)\n"
    "  --seed S        semente do RNG       (padrao 1)\n"
    "  --threads N     numero de threads    (padrao 1)\n"
    "                  as quatro varreduras O(N) de spr_spread sao repartidas\n"
    "                  em N faixas de linhas; as fases 1n3 e 5 seguem seriais\n"
    "  --variant V     A|B|C|none           (padrao A)\n"
    "                  as tres formas de proteger o read-modify-write sobre\n"
    "                  delta em spr_urbanize, na mesma compilacao (V6)\n"
    "                  A:    um mutex global\n"
    "                  B:    vetor de mutexes indexado pela celula alvo\n"
    "                  C:    compare-and-swap atomico, sem trava nenhuma\n"
    "                  none: SEM exclusao. Com --threads > 1 isto e uma\n"
    "                        corrida de dados de verdade, e existe so como\n"
    "                        demonstracao; a saida nao e confiavel\n"
    "  --rng M         anchored|legacy      (padrao anchored)\n"
    "                  anchored: sorteio ancorado a chave (semente, ano, fase,\n"
    "                  a, b, subpasso) — funcao pura, e o que permite V3.\n"
    "                  legacy:   ran1 do SLEUTH original, fluxo unico. E o\n"
    "                  oraculo de V7 e nao paraleliza.\n"
    "  --load DIR      le z/roads/excld/slp .txt de DIR em vez de gerar\n"
    "                  dados artificiais; as dimensoes vem do cabecalho\n"
    "  --coeff d,b,s,sr,rg\n"
    "                  difusao, breed, spread, resistencia a declividade e\n"
    "                  gravidade viaria (padrao 50,50,50,50,50)\n"
    "  --critical-slope V   declividade critica (padrao 21.0)\n"
    "  --scan ORDEM    forward|reverse (padrao forward). Inverte a ordem das\n"
    "                  linhas na varredura da fase 4. Instrumento de teste de\n"
    "                  V4: com --rng anchored o resultado NAO pode mudar.\n"
    "  --dump ARQUIVO  grava a matriz z final em texto\n"
    "  --hash          imprime o hash da matriz z ao fim de cada ano\n"
    "  --help\n", argv0);
}

/*
 * FNV-1a de 64 bits sobre a matriz z.
 *
 * E o instrumento central dos estagios S03 e S07: a invariante V3 diz que a
 * saida com N threads tem de ser identica a de 1 thread, e comparar um hash
 * por ano localiza o ano exato em que a divergencia aparece, sem despejar a
 * grade inteira. So quando diverge e que vale o dump completo.
 */
static unsigned long long
  hash_grid (GRID_P g, int n)
{
  unsigned long long h = 1469598103934665603ULL;
  int i;
  size_t b;

  for (i = 0; i < n; i++)
  {
    unsigned char *p = (unsigned char *) &g[i];
    for (b = 0; b < sizeof (PIXEL); b++)
    {
      h ^= (unsigned long long) p[b];
      h *= 1099511628211ULL;
    }
  }
  return h;
}

/* ------------------------------------------------------------------------ *
 * Dados artificiais
 *
 * Deterministicos, sem arquivo e sem RNG: a mesma grade em toda execucao.
 * A geracao configuravel (testgen.c) e do estagio S03.
 *
 * O objetivo aqui nao e realismo geografico, e sim exercitar as tres fases:
 * tem que haver semente urbana (para a fase 4 ter vizinhos), declividade
 * variada (para o teste de swght as vezes falhar), area excluida (para o
 * filtro de exclusao agir) e uma estrada (para a fase 5 ter onde caminhar).
 * ------------------------------------------------------------------------ */

static void
  build_artificial_input (void)
{
  int r, c;

  /* --- z: nucleo urbano central de 4x4 ---------------------------------- */
  for (r = g_nrows / 2 - 2; r < g_nrows / 2 + 2; r++)
    for (c = g_ncols / 2 - 2; c < g_ncols / 2 + 2; c++)
      if (r > 0 && r < g_nrows - 1 && c > 0 && c < g_ncols - 1)
        g_z[OFFSET (r, c)] = PHASE0G;

  /* --- slp: rampa de 0 a ~30, crescendo para o canto inferior direito --- */
  for (r = 0; r < g_nrows; r++)
    for (c = 0; c < g_ncols; c++)
      g_slp[OFFSET (r, c)] =
        (PIXEL) ((30 * (r + c)) / (g_nrows + g_ncols - 2));

  /* --- excld: faixa proibida de 2 colunas a 1/4 da largura -------------- */
  for (r = 0; r < g_nrows; r++)
    for (c = g_ncols / 4; c < g_ncols / 4 + 2; c++)
      g_excld[OFFSET (r, c)] = 100;   /* >= 100 zera o delta no filtro final */

  /*
   * --- roads: uma horizontal e uma vertical, valor MAX_ROAD_VALUE --------
   *
   * O valor 100 nao e decorativo. Em spr_road_walk:
   *
   *     run_value = (int) (roads[OFFSET (i, j)] / MAX_ROAD_VALUE
   *                        * diffusion_coefficient);
   *
   * roads[] e PIXEL (long) e MAX_ROAD_VALUE e 100, entao a divisao e
   * INTEIRA: qualquer estrada com valor abaixo de 100 da run_value = 0, a
   * caminhada termina no primeiro passo e a fase 5 praticamente nao exercita.
   * Com 100, run_value = diffusion_coefficient.
   *
   * Se isto e intencional no SLEUTH ou efeito colateral da divisao inteira e
   * questao em aberto — confirmar contra o dataset demo200 em S03 antes de
   * classificar como bug.
   */
  for (c = 0; c < g_ncols; c++)
    g_roads[OFFSET (g_nrows / 2, c)] = MAX_ROAD_VALUE;
  for (r = 0; r < g_nrows; r++)
    g_roads[OFFSET (r, g_ncols / 3)] = MAX_ROAD_VALUE;
}

/* ------------------------------------------------------------------------ *
 * Semente urbana do ano 0
 *
 * O original guarda a grade urbana de entrada e, ao fim de CADA ano, reescreve
 * PHASE0G nas celulas dela (growth.c:241-248):
 *
 *     seed_ptr = igrid_GetUrbanGridPtr (..., 0);
 *     util_condition_gif (total_pixels, seed_ptr, GT, 0, z_ptr, PHASE0G);
 *
 * Aqui a "semente" e o proprio z inicial, o que da no mesmo: o mkdata.sh ja
 * aplicou util_condition_gif(GT, 0, PHASE0G) ao converter o GIF urbano, e o
 * teste do original tambem e `> 0`. Guardar z antes do primeiro ano e guardar
 * a mesma coisa que o original guarda.
 *
 * Ver §D6 de specs/S03-harness.md e a nota de §B sobre por que isto e, na
 * pratica, um no-op sob a regra de merge atual.
 * ------------------------------------------------------------------------ */

static void
  capture_urban_seed (void)
{
  int i;

  for (i = 0; i < g_total_pixels; i++)
    g_urban_seed[i] = g_z[i];
}

static void
  restamp_urban_seed (void)
{
  util_condition_gif (g_total_pixels, g_urban_seed, GT, 0, g_z, PHASE0G);
}

/*
 * Declividade media da AREA URBANA INTEIRA — replica stats_circle
 * (stats_obj.c:2653-2677):
 *
 *     for (...) if (Z[OFFSET (i, j)] > 0) { addslope += slp[...]; number++; }
 *     *stats_average_slope = addslope / number;
 *
 * Nao confundir com o `average_slope` que spr_spread devolve, que e a media
 * sobre as celulas que cresceram NESTE ano. Sao numeros diferentes, e a
 * distincao custou uma correcao ao §D5 do spec do estagio: o original recebe
 * o average_slope de spr_spread em growth.c:204 e nunca o usa — nenhum
 * stats_Set* o consome. A coluna `slope` do avg.log e esta aqui.
 *
 * Roda uma vez por ano, fora de spr_spread, como o original. A cronometragem
 * de V11 (tarefa T8) tem de deixar esta chamada FORA da regiao medida.
 */
static double
  urban_mean_slope (void)
{
  double addslope = 0.0;
  int number = 0;
  int i;

  for (i = 0; i < g_total_pixels; i++)
  {
    if (g_z[i] > 0)
    {
      addslope += (double) g_slp[i];
      number++;
    }
  }
  return (number > 0) ? addslope / number : 0.0;
}

/*
 * O dump sai no MESMO formato que o --load consome: cabecalho "nrows ncols" e
 * depois as linhas. E deliberado — dump e entrada intercambiaveis significam
 * que a saida de uma execucao pode alimentar a proxima, e que os arquivos de
 * data/demo200/ sao diffaveis contra um dump.
 */
static void
  dump_grid (const char *path, GRID_P g)
{
  FILE *fp;
  int r, c;

  fp = fopen (path, "w");
  if (fp == NULL)
  {
    fprintf (stderr, "nao consegui abrir '%s' para escrita\n", path);
    exit (EXIT_FAILURE);
  }
  fprintf (fp, "%d %d\n", g_nrows, g_ncols);
  for (r = 0; r < g_nrows; r++)
  {
    for (c = 0; c < g_ncols; c++)
      fprintf (fp, "%ld%c", g[OFFSET (r, c)], (c == g_ncols - 1) ? '\n' : ' ');
  }
  fclose (fp);
}

/* ------------------------------------------------------------------------ *
 * Linha de comando
 * ------------------------------------------------------------------------ */

static int
  need_arg (int i, int argc, const char *flag)
{
  if (i + 1 >= argc)
  {
    fprintf (stderr, "opcao %s exige um argumento\n", flag);
    exit (EXIT_FAILURE);
  }
  return i + 1;
}

/*
 * --coeff d,b,s,sr,rg — a ordem e a mesma de sim_SetCoefficients: difusao,
 * breed, spread, resistencia a declividade, gravidade viaria.
 *
 * Cuidado ao conferir contra o avg.log do original: la as colunas saem na
 * ordem `diffus spread breed slp_res rd_grav`, com spread ANTES de breed. O
 * cenario de referencia e `--coeff 2,5,20,51,5` e aparece no avg.log como
 * `2 20 5 51 5`.
 */
static void
  parse_coeff (const char *s, options_t *o)
{
  if (sscanf (s, "%lf,%lf,%lf,%lf,%lf",
              &o->diffusion, &o->breed, &o->spread,
              &o->slope_resist, &o->road_gravity) != 5)
  {
    fprintf (stderr,
             "--coeff espera cinco numeros separados por virgula "
             "(d,b,s,sr,rg), recebi '%s'\n", s);
    exit (EXIT_FAILURE);
  }
}

static options_t
  parse_args (int argc, char **argv)
{
  options_t o;
  int i;

  o.rows = 20;
  o.cols = 20;
  o.dims_given = 0;
  o.years = 5;
  o.seed = 1;
  o.threads = 1;
  /*
   * PADRAO MUDOU NO S05 (T6). Ate o S04 era "none", que nao significava nada
   * porque nao havia thread. A partir daqui `none` com --threads > 1
   * significa CORRIDA DE DADOS DE VERDADE, e §D6 do spec do estagio e
   * explicito: o S05 nao pode entregar um programa com corrida de dados.
   *
   * Entao o padrao passa a ser a variante A, e `none` continua selecionavel
   * — explicitamente, nunca por omissao — como modo de demonstracao. Vale a
   * pena mante-lo: o §2.2 do esqueleto do TCC pede literalmente "um exemplo
   * de problema de sincronia", e ter o bug reproduzivel por flag vale mais
   * texto do que descreve-lo.
   */
  o.variant = "A";
  /*
   * Padrao `anchored`, conforme §D7 do spec do estagio. O prototipo que o
   * trabalho entrega e o paralelo; `legacy` e modo de compatibilidade com o
   * oraculo de V7. Deixar o padrao no modo que NAO escala convidaria a medir
   * a coisa errada em S08.
   */
  o.rng_mode = "anchored";
  o.scan_order = "forward";
  o.dump_path = NULL;
  o.load_dir = NULL;
  o.want_hash = 0;

  /*
   * Coeficientes medianos na escala 0-100, herdados do estagio S02: escolhidos
   * para que as quatro regras produzam crescimento visivel numa grade pequena.
   * Nao tem nada a ver com calibracao, que esta fora de escopo (§C).
   */
  o.diffusion      = 50.0;
  o.breed          = 50.0;
  o.spread         = 50.0;
  o.slope_resist   = 50.0;
  o.road_gravity   = 50.0;
  o.critical_slope = 21.0;

  for (i = 1; i < argc; i++)
  {
    if (strcmp (argv[i], "--rows") == 0)
    {
      o.rows = atoi (argv[i = need_arg (i, argc, "--rows")]);
      o.dims_given = 1;
    }
    else if (strcmp (argv[i], "--cols") == 0)
    {
      o.cols = atoi (argv[i = need_arg (i, argc, "--cols")]);
      o.dims_given = 1;
    }
    else if (strcmp (argv[i], "--load") == 0)
      o.load_dir = argv[i = need_arg (i, argc, "--load")];
    else if (strcmp (argv[i], "--coeff") == 0)
      parse_coeff (argv[i = need_arg (i, argc, "--coeff")], &o);
    else if (strcmp (argv[i], "--critical-slope") == 0)
      o.critical_slope = atof (argv[i = need_arg (i, argc, "--critical-slope")]);
    else if (strcmp (argv[i], "--years") == 0)
      o.years = atoi (argv[i = need_arg (i, argc, "--years")]);
    else if (strcmp (argv[i], "--seed") == 0)
      o.seed = atol (argv[i = need_arg (i, argc, "--seed")]);
    else if (strcmp (argv[i], "--threads") == 0)
      o.threads = atoi (argv[i = need_arg (i, argc, "--threads")]);
    else if (strcmp (argv[i], "--variant") == 0)
      o.variant = argv[i = need_arg (i, argc, "--variant")];
    else if (strcmp (argv[i], "--rng") == 0)
      o.rng_mode = argv[i = need_arg (i, argc, "--rng")];
    else if (strcmp (argv[i], "--scan") == 0)
      o.scan_order = argv[i = need_arg (i, argc, "--scan")];
    else if (strcmp (argv[i], "--dump") == 0)
      o.dump_path = argv[i = need_arg (i, argc, "--dump")];
    else if (strcmp (argv[i], "--hash") == 0)
      o.want_hash = 1;
    else if (strcmp (argv[i], "--help") == 0)
    {
      usage (argv[0]);
      exit (EXIT_SUCCESS);
    }
    else
    {
      fprintf (stderr, "opcao desconhecida: %s\n", argv[i]);
      usage (argv[0]);
      exit (EXIT_FAILURE);
    }
  }

  /*
   * --rng e validado com rigor, e nao apenas avisado como --threads e
   * --variant. A diferenca e que um valor invalido aqui nao pode cair num
   * padrao silencioso: os dois modos produzem numeros DIFERENTES por
   * construcao (§D6 do spec), entao um `--rng lecacy` digitado errado
   * devolveria uma corrida `anchored` com cara de corrida `legacy`, e a
   * comparacao de V7 acusaria uma divergencia inexistente. Falhar cedo custa
   * menos do que depurar isso.
   */
  if (strcmp (o.rng_mode, "anchored") != 0 &&
      strcmp (o.rng_mode, "legacy") != 0)
  {
    fprintf (stderr, "--rng aceita 'anchored' ou 'legacy', recebi '%s'\n",
             o.rng_mode);
    exit (EXIT_FAILURE);
  }

  if (strcmp (o.scan_order, "forward") != 0 &&
      strcmp (o.scan_order, "reverse") != 0)
  {
    fprintf (stderr, "--scan aceita 'forward' ou 'reverse', recebi '%s'\n",
             o.scan_order);
    exit (EXIT_FAILURE);
  }

  /*
   * A partir do S05 o --threads deixou de ser ignorado, entao ele passa a ser
   * validado com o mesmo rigor do --rng, e pelo mesmo motivo: antes um valor
   * absurdo so produzia um aviso, agora ele decide como o ano e repartido.
   * `--threads 0` ou negativo nao tem leitura razoavel — calar e cair no
   * padrao esconderia um erro de digitacao numa tabela de desempenho.
   */
  if (o.threads < 1)
  {
    fprintf (stderr, "--threads exige um inteiro >= 1, recebi '%d'\n",
             o.threads);
    exit (EXIT_FAILURE);
  }

  /*
   * As tres variantes de V6 passaram a existir no S06, e a recusa com nome
   * proprio que vivia aqui ("B e C entram no estagio S06") saiu junto.
   */
  if (par_variant_code (o.variant) < 0)
  {
    fprintf (stderr, "--variant aceita 'A', 'B', 'C' ou 'none', recebi '%s'\n",
             o.variant);
    exit (EXIT_FAILURE);
  }

  return o;
}

/* ------------------------------------------------------------------------ */

int
  main (int argc, char **argv)
{
  options_t opt;
  int year;
  float average_slope;
  int num_growth_pix;
  int sng, sdc, og, rt, pop;

  opt = parse_args (argc, argv);

  /*
   * O aviso e alto porque a situacao e grave: `none` com mais de uma thread
   * e o read-modify-write sobre delta desprotegido, isto e, comportamento
   * indefinido — nao "um resultado um pouco diferente". Quem pedir isso tem
   * de saber que pediu.
   */
  if (strcmp (opt.variant, "none") == 0 && opt.threads > 1)
    fprintf (stderr,
      "AVISO: --variant none com %d threads deixa o read-modify-write sobre\n"
      "       delta SEM exclusao mutua. E corrida de dados, e o resultado nao\n"
      "       e confiavel. Modo de demonstracao; use --variant A para medir.\n",
      opt.threads);

  if (opt.load_dir != NULL)
  {
    if (opt.dims_given)
      fprintf (stderr,
        "aviso: --rows/--cols ignorados; com --load as dimensoes vem do "
        "cabecalho dos arquivos\n");
    load_input_dir (opt.load_dir);   /* faz o grid_alloc por dentro */
  }
  else
  {
    grid_alloc (opt.rows, opt.cols);
    build_artificial_input ();
  }

  /*
   * par_init vem DEPOIS da alocacao das grades porque a faixa de cada worker
   * e calculada a partir de g_nrows, e ANTES do primeiro ano porque a
   * invariante V9 nao admite alocacao dentro do laco de fases: o vetor de
   * workers e a barreira nascem aqui e duram a corrida inteira.
   */
  par_init (opt.threads, par_variant_code (opt.variant));

  /* o z inicial E a semente do ano 0; ver capture_urban_seed */
  capture_urban_seed ();
  stats_Reset ();

  /*
   * Substitui a chamada direta a InitRandom do estagio S02. rng_init semeia o
   * ran1 NOS DOIS MODOS e registra qual deles vale; ver rng.c para por que o
   * ran1 e semeado mesmo quando nao sera usado.
   */
  rng_init ((strcmp (opt.rng_mode, "legacy") == 0) ? RNG_LEGACY : RNG_ANCHORED,
            opt.seed);

  g_scan_reverse = (strcmp (opt.scan_order, "reverse") == 0);

  /*
   * Coeficientes fixos durante toda a corrida. No SLEUTH eles vem do arquivo
   * de cenario e sao reajustados ano a ano por coeff_SelfModication; aqui nao
   * ha automodificacao, e no cenario de referencia ela esta neutralizada
   * (§D4 de specs/S03-harness.md), de modo que os dois lados usam os mesmos
   * cinco numeros nos 20 anos.
   */
  sim_SetCoefficients (opt.diffusion,
                       opt.breed,
                       opt.spread,
                       opt.slope_resist,
                       opt.road_gravity);
  sim_SetCriticalSlope (opt.critical_slope);

  /*
   * O modo do gerador sai no cabecalho por exigencia do §T2 do spec: os dois
   * modos dao numeros diferentes por construcao, entao uma saida que nao diga
   * em qual deles foi colhida e ambigua — e saidas deste programa viram
   * tabela do texto do TCC.
   */
  /*
   * A palavra fixa "serial" saiu e entrou a contagem de threads. O motivo e o
   * mesmo que pos o modo do gerador aqui no S04: saidas deste programa viram
   * tabela do texto do TCC, e uma tabela de speedup em que nao se le quantas
   * threads produziram cada linha nao vale nada. Com uma thread sai
   * "1 thread", nao "serial" — sao a mesma execucao, mas quem le a coluna
   * quer o numero.
   */
  printf ("sleuth-par | grade %dx%d | %d anos | semente %ld | %d thread%s"
          " | variante %s | rng %s%s | %s\n",
          g_nrows, g_ncols, opt.years, opt.seed,
          g_threads, (g_threads == 1) ? "" : "s", par_variant_name (),
          rng_mode_name (),
          g_scan_reverse ? " | scan reverse" : "",
          (opt.load_dir != NULL) ? opt.load_dir : "entrada artificial");
  printf ("coeficientes: difusao %g breed %g spread %g slp_res %g rd_grav %g"
          " | declividade critica %g\n",
          opt.diffusion, opt.breed, opt.spread, opt.slope_resist,
          opt.road_gravity, opt.critical_slope);
  /*
   * `cresc_decl` e a media de declividade das celulas que cresceram no ano —
   * o average_slope que spr_spread devolve. `decl_z` e a media sobre a area
   * urbana inteira, que e o que o avg.log do original chama de `slope`. O
   * original calcula os dois e so reporta o segundo; ver urban_mean_slope.
   */
  printf ("%4s %7s %7s %7s %7s %9s %9s %10s %11s",
          "ano", "sng", "sdc", "og", "rt", "cresc", "pop",
          "cresc_decl", "decl_z");
  printf (opt.want_hash ? " %18s\n" : "\n", "hash(z)");

  for (year = 1; year <= opt.years; year++)
  {
    sng = sdc = og = rt = 0;
    num_growth_pix = 0;
    average_slope = 0.0f;

    sim_SetCurrentYear (year);

    spr_spread (&average_slope, &num_growth_pix,
                &sng, &sdc, &og, &rt, &pop);

    /*
     * growth.c:241-248, o unico item do laco anual do original que faltava
     * aqui. Vem depois de spr_spread e antes do ano seguinte.
     */
    restamp_urban_seed ();

    /*
     * decl_z sai com seis casas de proposito. O avg.log do original publica
     * duas, entao a comparacao de V7 so pode ser feita a menos de 0,005 — e
     * arredondar tambem do nosso lado antes de comparar introduz um SEGUNDO
     * arredondamento, que ja produziu uma falha falsa: no ano 1997 o valor
     * verdadeiro fica em ~2,4147; com tres casas vira "2.415", e dai o gawk
     * (que arredonda o decimal para cima) le 2,42 enquanto o C do original
     * arredondou o binario para 2,41. Nenhum dos dois estava errado; o erro
     * era arredondar duas vezes.
     */
    printf ("%4d %7d %7d %7d %7d %9d %9d %10.3f %11.6f",
            year, sng, sdc, og, rt, num_growth_pix, pop,
            (double) average_slope, urban_mean_slope ());
    if (opt.want_hash)
      printf (" %018llu", hash_grid (g_z, g_total_pixels));
    printf ("\n");
  }

  {
    stats_counters_t s = stats_Get ();
    printf ("\ntentativas de urbanizacao (acumulado):\n");
    printf ("  sucesso             %ld\n", s.urban_success);
    printf ("  ja era urbana (z)   %ld\n", s.z_failure);
    printf ("  ja crescida (delta) %ld   <- a leitura do read-modify-write\n",
            s.delta_failure);
    printf ("  declividade         %ld\n", s.slope_failure);
    printf ("  area excluida       %ld\n", s.excluded_failure);
  }

  if (opt.dump_path != NULL)
    dump_grid (opt.dump_path, g_z);

  par_finish ();
  grid_free ();
  return EXIT_SUCCESS;
}
