# S03 — harness de teste

**Estágio:** S03 · **Status:** concluído com desvio (`~`) · **Dia no plano:** 17/09 (escrito em 18/09)
**Mestre:** `../SPEC.md` · **Cita:** V7, V11, V15
**Alimenta o texto:** §4.1 (metodologia de verificação) e §3.1 (tabela de divergências)

**O desvio, em uma frase:** T7, T8 e T11 — as três tarefas de infraestrutura de
*benchmark* — foram movidas para o S08, que é o único estágio que depende
delas; com elas saem VS12 e VS13, as duas únicas verificações do §V que não
rodam. Justificativa em §T; consequência registrada em `run_checks.sh`, que
imprime as duas como adiadas em vez de omiti-las.

---

## §G objetivo do estágio

Construir a infraestrutura que permite **afirmar que o protótipo está correto** e
**medir quanto ele custa**, antes de existir qualquer thread.

Concretamente, três coisas que hoje não existem:

1. um caminho para alimentar o `sleuth-par` com os **dados reais do `demo200`**,
   sem colocar leitura de GIF no núcleo (§C proíbe);
2. uma comparação **automatizada** contra o `grow` original — a primeira
   verificação de fato da invariante V7, que hoje é só uma aposta;
3. geração de dados artificiais **configurável e que escale**, mais uma
   cronometragem que obedeça V11, para servir de base ao speedup de S08.

Este estágio não paraleliza nada e não muda o RNG. Ele existe porque V7 é a
fundação de V3: se a versão serial não bate com o original, comparar 1 thread
contra 4 threads mede a consistência de um erro.

## §S escopo

**Entra:** `harness/` (conversor GIF→texto, construção do baseline de
referência, cenário de referência), `--load` e `--coeff` no `sleuth-par`,
`testgen.c`, cronometragem V11, script de comparação V7, extensão de
`tests/run_checks.sh`.

**Não entra:** threads (S05/S06), RNG ancorado à célula (S04), as medições de
speedup propriamente ditas (S08), calibração e Lee-Sallee (§C), leitura de GIF
**dentro** do núcleo (§C — ver §D2 sobre como o conversor fica de fora).

---

## §D entradas conhecidas

Levantamento feito em 18/09 sondando o ambiente e o código real, antes de
escrever este spec (§R1). Nada aqui precisa ser redescoberto — e vários itens
contradizem o que era suposto.

### D1 — o `demo200`, medido

Todas as grades são **200×200**. Levantadas com o conversor de §D2, contando
ocorrência de cada valor:

| grade | valores | observação |
|---|---|---|
| `urban.1930/1950/1970/1990` | binário 0/255 | 598 / 861 / 1821 / **3881** células |
| `roads.1930/1950/1970/1990` | binário 0/255 | 375 / 665 / 945 / **1104** células |
| `slope` | 0 a 41 | 10722 células em zero |
| `excluded` | só 1 e 100 | 35775 em 1, **4225 em 100** |
| `hillshade`, `landuse`, `actlanduse` | — | fora de escopo (fundo e deltatron) |

O `excluded` não tem valor intermediário: o teste `>= 100` do núcleo divide a
grade em exatamente duas classes. E o `slope` chega a 41, acima do
`CRITICAL_SLOPE=15.0` do cenário — o filtro de declividade age de verdade.

### D2 — não há conversor de GIF no ambiente; o núcleo continua sem ler GIF

Sondado: **não** existe `PIL`, `numpy`, ImageMagick, netpbm nem `gif2rgb` no
WSL. Converter fora do C não é opção.

A saída é um utilitário de harness, `gif2asc`, que linka **apenas**
`SLEUTH/GD/gd.c` (mais `mtables.c`, que o `gd.c` inclui por `#include`) e
replica literalmente `gdif_obj.c:359-380`:

```c
index_val = gdImageGetPixel (im, j, i);
red = gdImageRed (im, index_val);
if (red == green && red == blue)  grid[OFFSET (i, j)] = red;   /* senão: erro */
```

Isso **já foi compilado e executado** durante a sondagem: gera os números de
§D1. `gcc -std=gnu89 -w` é necessário — o `gd.c` é de 1994.

O ponto de projeto: o conversor é uma **ferramenta de harness**, não parte do
protótipo. O núcleo lê texto. §C proíbe o subsistema `gdif_obj`/`GD` *no
protótipo*, e isso continua valendo — nenhum `.o` do GD entra no binário
`sleuth-par`. A conversão é um passo de preparação de dados, roda uma vez, e o
resultado vai versionado como texto.

### D3 — o pré-processamento do original, e o contrato do carregador

Depois de ler os GIFs, o original transforma **só as estradas**
(`main.c:394 → igrid_NormalizeRoads`). `igrid_ValidateGrids` e
`igrid_VerifyInputs` apenas conferem e registram; não mutam nada.

```c
/* igrid_obj.c:722-727 */
image_max   = igrid.road[i].max;
norm_factor = image_max / max_of_max;
grid_ptr[j] = (PIXEL) (((100.0 * grid_ptr[j]) / image_max) * norm_factor);
```

Com uma única grade de estrada carregada (ver §B4), `image_max = max_of_max = 255`,
`norm_factor = 1.0`, e toda célula de estrada vira **exatamente 100**.

Contrato do carregador, então:

| matriz | origem | transformação |
|---|---|---|
| `g_z` | `demo200.urban.1990.gif` | `PHASE0G` onde > 0, senão 0 |
| `g_roads` | `demo200.roads.1990.gif` | 100 onde 255, 0 onde 0 |
| `g_excld` | `demo200.excluded.gif` | crua (1 e 100) |
| `g_slp` | `demo200.slope.gif` | crua (0..41) |

Uma grade de cada. Em modo `predict` o original carrega só as grades urbanas
com ano ≥ `PREDICTION_START_DATE` (`igrid_obj.c:1237`) — com início em 1990,
sobra apenas a de 1990. É por isso que a semente tem 3881 células, e é o que
fecha a aritmética: `pop(1991) = 3881 + grw_pix(1991) = 3881 + 86 = 3967`,
exatamente o que o `avg.log` mostra.

### D4 — a receita do cenário de referência (provada, `exit 0`)

Derivada de `Scenarios/scenario.demo200_predict`. Cada linha existe por um
motivo, e três delas contornam defeitos do original (§B1–§B3):

| ajuste | por quê |
|---|---|
| remover todas as linhas `LANDUSE_DATA` | leva ao caminho `grw_non_landuse`, sem deltatron (§C) |
| `MONTE_CARLO_ITERATIONS=1` | o `avg.log` passa a ser a corrida única, não uma média de 10 |
| `CRITICAL_LOW=0.0`, `CRITICAL_HIGH=1e13` | neutraliza `coeff_SelfModication` — **a receita vem comentada no próprio cenário, linhas 428-429** |
| `WRITE_MEMORY_MAP=YES` | contorna o `fclose(NULL)` de `mem_Init` (§B2) |
| `LOGGING=NO` | contorna o estouro de `landclass_LogIt` (§B3) |
| `ECHO=no`, `ANIMATION=no` | ruído e dependência do `whirlgif` |
| `INPUT_DIR`, `OUTPUT_DIR` absolutos | o cenário original usa caminhos relativos a `SLEUTH/` |

Mais as variáveis de ambiente `USER`, `HOST`, `HOSTTYPE`, `OSTYPE` — sem elas o
`grow` morre em `SIGSEGV` e o *handler* imprime uma lista de variáveis de 2001
como se fosse a causa. Não é a causa, é só a dica que o handler sabe dar; mas
defini-las é necessário de todo modo.

Coeficientes de predição do cenário, que o `--coeff` de T5 precisa reproduzir:
**diffusion 2, breed 5, spread 20, slope_resist 51, road_gravity 5**, com
`CRITICAL_SLOPE=15.0`, `RANDOM_SEED=1`, 1990 → 2010.

Que a neutralização funcionou é **verificável na própria saída**: as colunas
`diffus spread breed slp_res rd_grav` do `avg.log` ficaram constantes em
`2 20 5 51 5` nos 20 anos. Isso é a VS10.

### D5 — `avg.log` é a superfície de comparação de V7, sem instrumentar nada

Com `WRITE_AVG_FILE=yes` e 1 Monte Carlo, o original escreve uma linha por ano:

```
  run year index     sng      sdg      sdc       og       rt      pop  ...  slope  ...  grw_pix
    0 1991  0       1.00     0.00     0.00    81.00     4.00  3967.00  ...   2.44  ...    86.00
```

Sete dessas colunas são exatamente o que o `sleuth-par` imprime por ano. **O
mapeamento não é o óbvio** — duas armadilhas, §B5 e §B11:

| `sleuth-par` | coluna do `avg.log` | campo (`awk`) | observação |
|---|---|---|---|
| `sng` | `sng` | `$4` | direto |
| `sdc` | **`sdg`** | **`$5`** | ⚠ não a coluna `sdc` — §B5 |
| `og` | `og` | `$7` | direto |
| `rt` | `rt` | `$8` | direto |
| `pop` | `pop` | `$9` | direto |
| **`decl_z`** (`urban_mean_slope`) | `slope` | `$16` | ⚠ **não** é o `average_slope` de `spr_spread` — §B11 |
| `cresc` (`num_growth_pix`) | `grw_pix` | `$27` | direto |
| `cresc_decl` (`average_slope`) | — | — | **sem contrapartida**: o original calcula e descarta (§B11) |
| — | `sdc` | `$6` | deve ser **identicamente 0** em todos os anos (§B5) |
| — | `diffus spread breed slp_res rd_grav` | `$18`–`$22` | constantes: é a VS10 |

Os números de campo estão aqui porque **o cabeçalho tem 27 colunas e é fácil
errar por um**: `run year index` ocupam `$1`–`$3` antes de `sng` começar em
`$4`. Conferidos contra a saída real; errei esse deslocamento na primeira
tentativa do alvo `smoke` e o `awk` imprimiu `cl_size` no lugar de `diffus`.

Isso dispensa instrumentar a cópia de referência para despejar `z`. 7 números ×
20 anos batendo exatamente é evidência forte. Se **não** bater, aí sim vale
instrumentar e comparar `z` ano a ano — mas como ferramenta de diagnóstico, não
como porta do estágio.

**Resultado (18/09):** bate. As seis estatísticas inteiras são exatamente
iguais nos 20 anos e a declividade concorda dentro das duas casas que o
`avg.log` publica. Ver §V.

### D6 — o que o laço anual do original faz além de `spr_spread`

`growth.c:147-265`. Em ordem, por ano:

1. `spr_spread(...)` — o núcleo;
2. `grw_non_landuse(z)` — acumula Monte Carlo; **não escreve em `z`**;
3. `util_condition_gif(seed, GT, 0, z, PHASE0G)` — **re-estampa a semente urbana
   do ano 0 em `z`** (`growth.c:241-248`);
4. `stats_Update(num_growth_pix)`;
5. `coeff_SelfModication(...)` — neutralizada por §D4.

O item 3 é o que o `sleuth-par` **não** faz hoje. Entra como T6.

**Correção, 18/09:** a frase original deste parágrafo dizia que a
re-estampagem "muda o código de fase gravado nas células da semente, e
portanto o conteúdo de `z` byte a byte". **Não muda** — é um no-op sob a regra
de merge do modelo. Ver §B12. T6 foi implementada assim mesmo, porque
fidelidade ao laço anual do original vale mais que a economia de um passe por
ano, e porque um no-op *hoje* deixa de ser no-op se a regra de merge mudar.

### D7 — o RNG é idêntico, e é semeado uma única vez

Duas sondagens que desriscam V7 no ponto que mais importava:

- **Sequência bit a bit idêntica.** Linkando `sleuth-par/random.c` e
  `SLEUTH/random.c` contra o mesmo driver, os 12 primeiros valores para semente
  1 são iguais; o primeiro é `0.091964890757559287`. (O `random.c` do SLEUTH
  precisa de stubs para `glb_call_stack`, `glb_call_stack_index` e `glb_i`, e de
  `glb_call_stack_index` iniciado acima de zero, senão o `FUNC_END` de
  `InitRandom` desce abaixo de zero e aborta.)
- **`InitRandom` é chamado uma vez**, em `main.c:519`, antes de `drv_driver()`.
  E `driver.c` **não toca no RNG**. Logo o ano 1 começa na posição 0 do fluxo,
  igual ao `sleuth-par`.

### D8 — a cronometragem muda o desenho de S08

Medido, 20 anos em 200×200, mediana de 3: `grow` **0,10 s**, `sleuth-par`
**0,01 s**. Escala do `sleuth-par` (20 anos, semente 1):

| grade | tempo | pop final | % urbano |
|---|---|---|---|
| 200×200 | 0,01 s | 3 114 | 7,8 % |
| 500×500 | 0,09 s | 7 039 | 2,8 % |
| 1000×1000 | 0,56 s | 13 371 | 1,3 % |
| 2000×2000 | 3,71 s | 26 636 | 0,67 % |

Três consequências:

1. **O baseline de *desempenho* é o `sleuth-par` com 1 thread, não o `grow`.** O
   `grow` é 10× mais lento porque escreve GIF e calcula estatísticas que estão
   fora de escopo; comparar contra ele infla o speedup artificialmente. §I.baseline
   do mestre já diz que o `grow` é "referência normativa de **corretude**" — este
   estágio torna a distinção operacional.
2. S08 precisa de **≥ 2000×2000**, ou 1000×1000 com muitos anos. Confirma V15 e §B6
   com número em vez de intuição.
3. A tabela acima expõe o defeito de §B7: a fração urbana **cai** com o tamanho da
   grade, porque a semente não escala. Um benchmark em 2000×2000 hoje mede
   sobretudo varredura de grade vazia.

---

## §T tarefas

| id | st | tarefa | cita |
|----|----|--------|------|
| T1 | x | `harness/Makefile`: alvo que copia `SLEUTH/` para `build/sleuth-ref/` (fora da árvore, §R2), aplica o patch de §B1, constrói `GD/` e `grow`, e expõe um *wrapper* que define `USER`/`HOST`/`HOSTTYPE`/`OSTYPE`. Baseline **durável**, versionado como receita e não como binário — ver §B6. | I.baseline |
| T2 | x | `harness/gif2asc.c`: GIF em tons de cinza → texto, replicando `gdif_obj.c:359-380`. Linka só `gd.c`. Modo `--stats` para reproduzir §D1. | §C |
| T3 | x | `harness/mkdata.sh`: converte as quatro grades de §D3 para `data/demo200/*.txt`, aplicando a normalização de estradas. Formato: cabeçalho `nrows ncols`, depois as linhas — o mesmo que `--dump` produz, para que dump e entrada sejam intercambiáveis. | — |
| T4 | x | `--load DIR` no `sleuth-par`: lê as quatro grades, tira `g_nrows`/`g_ncols` do cabeçalho, monta `g_z` como `PHASE0G` onde urbano > 0. Alocação única, antes do laço (V9). **Estende `I.cli`** — ver §N. Feito em `load.[ch]`, módulo próprio; o `z.txt` já vem estampado, então o carregador não transforma nada (§B13). | V7, V9 |
| T5 | x | `--coeff d,b,s,sr,rg` e `--critical-slope V`: hoje os coeficientes estão fixos em `50,50,50,50,50` e 21.0 no `main.c`. Sem isso não há como reproduzir `2,5,20,51,5` / 15.0 de §D4. **Estende `I.cli`**. | V7 |
| T6 | ~ | Re-estampar a semente urbana em `z` como `PHASE0G` ao fim de cada ano, replicando `growth.c:241-248` (§D6). Guardar a semente original numa sexta grade global (`g_urban_seed`). **Desvio:** implementada e medida como **no-op** — ver §B12. | V7 |
| T7 | → | **Movida para o S08.** `testgen.c`: tirar a geração artificial do `main.c` e torná-la configurável — fração urbana **proporcional à área**, distribuição (aglomerado central / múltiplos núcleos / uniforme), perfil de declividade, fração excluída, densidade da malha de estradas. Determinística a partir da semente. Corrige §B7. | V15 |
| T8 | → | **Movida para o S08.** Cronometragem V11: `clock_gettime(CLOCK_MONOTONIC)` em volta **apenas** do laço de anos — fora da geração/carga de dados, do dump e do hash. `--repeat N` e relato da **mediana**. | V11 |
| T9 | x | `tests/compare_v7.sh`: roda a referência e o `sleuth-par` em 1990→2010, extrai as 7 colunas de §D5 de cada lado e compara. Em caso de divergência, imprimir o **primeiro ano** divergente e as duas linhas. Também exposto como `make v7`. | V7 |
| T10 | x | `_Static_assert (sizeof (PIXEL) >= 2 * sizeof (int))` em `globals.h`, mais asserção de que `growth_count <= total_pixels`. Ver §B8. | V9 |
| T11 | → | **Movida para o S08.** Registrar o baseline serial cronometrado (grades × anos, mediana de 5) em `harness/baseline.md`, para S08 comparar contra número escrito, não contra memória. | V11, V15 |
| T12 | x | Estender `tests/run_checks.sh` com a seção S03 (VS8–VS14), mantendo VS1–VS7 e o check B7 passando. | — |

Status: `.` pendente · `x` concluído · `~` concluído com desvio · `→` movida
para outro estágio

Ordem sugerida: T2 → T3 → T1 → T4 → T5 → T6 → T9 (fecha V7, que é o risco do
estágio) e depois T10 → T12.

### Por que T7, T8 e T11 saíram

As três são infraestrutura de **benchmark**, e a dependência é de mão única:
**nada em S04–S07 as consome.** O S04 troca o RNG, o S05 e o S06 paralelizam,
o S07 compara 1 contra N threads — e todos verificam corretude, contra o
`demo200` e contra o `avg.log` do original, que já existem. Quem precisa de
`testgen` que escale (T7), de cronometragem por mediana (T8) e de um baseline
escrito (T11) é o **S08**, o estágio de desempenho.

Mantê-las aqui custaria algumas horas no caminho crítico da paralelização,
que é o que está atrasado, para produzir artefatos que ficariam parados até o
dia 23. Movidas, elas chegam ao S08 junto com o contexto que as usa.

O preço, declarado: o S03 fecha como `~` e não como `x`, e VS12/VS13 ficam
sem executar. O `run_checks.sh` as imprime como adiadas — ver §V.

**Estado em 18/09 (fechamento):** T1–T6, T9, T10 e T12 concluídas; T7, T8 e
T11 movidas para o S08. VS8–VS11 e VS14 passando, VS12 e VS13 adiadas. O
estágio fecha com o risco — V7 — resolvido, que era a razão de ele existir.

## §V verificação do estágio

VS1–VS7 de S02 continuam valendo. **Todas as que executam estão automatizadas
em `tests/run_checks.sh`** desde T12 — antes dela, VS8–VS11 e VS14 eram
verificações feitas à mão, e uma verificação que depende de alguém lembrar de
rodá-la não é uma porta de estágio.

Uma rodada completa, ao fechamento:

```
== S02: verificacao ==          VS1..VS6, B7, VS7      ok
== S03: verificacao ==          VS8 VS9 VS10 VS11 VS14 ok
                                VS12 VS13              adiadas para o S08
== tudo passou, com 2 verificacao(oes) adiada(s) para o S08 ==
```

- **VS8** — `gif2asc --stats` reproduz exatamente as contagens de §D1 para as
  quatro grades. Automatizada sobre as **dez** grades de §D1 (quatro urbanas,
  quatro de estrada, `slope` e `excluded`): para as binárias compara a
  contagem de não-zero; para `slope` e `excluded`, que não são binárias,
  compara o histograma, que é mais forte.
- **VS9** — o `grow` de referência roda 1990→2010 com `exit 0` e escreve um
  `avg.log` com 20 linhas de dados.
- **VS10** — as colunas `diffus spread breed slp_res rd_grav` do `avg.log` são
  constantes nos 20 anos (automodificação neutralizada — §D4).
- **VS11** — **V7:** as 7 colunas de §D5 batem entre `sleuth-par --load` e o
  `avg.log`, em todos os 20 anos. A coluna `sdc` do `avg.log` é 0 em todos.
  **PASSA** (18/09), por `tests/compare_v7.sh` / `make v7`:

  ```
  ok    20 anos de cada lado
  ok    coluna sdc do avg.log identicamente zero (§B5 confirmado)
  ok    sng sdc og rt cresc pop decl_z batem nos 20 anos
  ```

  As seis estatísticas inteiras batem **exatamente**. A declividade é
  comparada com tolerância de 0,005 — não por frouxidão, mas porque é a
  precisão que o `avg.log` publica; comparar por igualdade de string exige
  arredondar também o nosso lado, e o arredondamento duplo produziu uma falha
  falsa (§B14). O comparador foi verificado contra um caso negativo
  (`COEFF=3,5,20,51,5 ./tests/compare_v7.sh` acusa e localiza o primeiro ano).
- **VS12** — **V11:** a região cronometrada exclui I/O. Verificável: `--dump` e
  `--hash` não alteram o tempo relatado além do ruído, e `--repeat 5` reporta
  mediana. **ADIADA para o S08**, com T8 — não há o que verificar enquanto
  `--repeat` não existir.
- **VS13** — a fração urbana final do `testgen` fica dentro de uma faixa
  estreita entre 200×200 e 2000×2000 (hoje varia de 7,8 % a 0,67 % — §D8).
  **ADIADA para o S08**, com T7.
- **VS14** — determinismo preservado com `--load`: duas execuções com a mesma
  semente dão dump idêntico; sementes diferentes divergem. (Extensão de VS6.)
  **PASSA** (18/09), junto com valgrind limpo sob `--load` (0 erros, 0
  vazamentos) e com a intercambialidade dump↔entrada: o dump de `z` de uma
  execução realimenta o `--load` de outra. Automatizada em T12, incluindo a
  realimentação — que é o que impede §B13 de regredir.

**Sobre VS12 e VS13 aparecerem no relatório.** O `run_checks.sh` as imprime
com marcador `--` e conta quantas foram adiadas, em vez de simplesmente não
mencioná-las. É deliberado: §V12 do mestre manda declarar desvio em vez de
silenciá-lo, e uma verificação que some da saída é uma verificação que ninguém
lembra de retomar. Quando o S08 implementar T7 e T8, as duas linhas já estão
lá esperando.

### Se VS11 falhar, nesta ordem de suspeita

1. **Mapeamento de colunas** — comparar `sdc` contra a coluna `sdc` em vez de
   `sdg` (§B5). É o erro mais provável e o mais fácil de confundir.
2. **A re-estampagem de T6** ausente ou em lugar errado no laço.
3. **Coeficientes ou `CRITICAL_SLOPE`** não batendo com §D4.
4. **Normalização de estradas** — se as estradas não ficarem exatamente 100, a
   fase 5 desliga (§B9) e `rt` vai a zero. `rt` divergente com o resto batendo
   aponta direto para cá.
5. **B7** — improvável: `grw_pix` máximo em 20 anos é 117, contra `nrows = 200`
   (§B10). Mas a margem é de só 1,7×.
6. **`util_get_next_neighbor`** (B12 do mestre) — o `static last_index` é estado
   entre chamadas; se o `sleuth-par` o reinicializar em ponto diferente do
   original, a sequência de vizinhos desanda.

**Retrospecto (18/09).** A lista acertou o primeiro lugar e errou o motivo. A
única divergência real foi de **mapeamento de colunas** — mas não a de `sdc`
contra `sdg`, que estava catalogada e por isso não pegou ninguém: foi a coluna
`slope`, que este spec afirmava ser o `average_slope` de `spr_spread` e não é
(§B11). Os itens 2 a 6 nunca chegaram a ser exercidos. O valor da lista foi
menos o conteúdo dela e mais o hábito de olhar primeiro para o que se está
comparando, antes de desconfiar do que se está computando.

---

## §B achados do estágio

Todos de 18/09: §B1–§B10 saíram da sondagem que precedeu este spec, §B11–§B14
da execução de T4–T9 na mesma noite. §B1–§B5, §B9 e §B11 são defeitos ou
características do **SLEUTH original**; §B6–§B8 e §B12–§B14 são do nosso lado.

| id | achado | consequência |
|----|--------|--------------|
| B1 | `util_WriteZProbGrid` (`utilities.c:798`) declara `char date_str[4]` e faz `sprintf (date_str, "%u", ano)`. `"1991"` + NUL = **5 bytes**: estouro de pilha de um byte, em **todo** ano de quatro dígitos. `grw_grow` (`growth.c:73`) e `grw_landuse` (`:360`) declaram `date_str[5]`; esta função é a exceção. | O `_FORTIFY_SOURCE` do glibc moderno aborta o processo. **O SLEUTH original não roda até o fim em modo `predict` num toolchain atual.** Sem contorno por cenário: a chamada é incondicional (`growth.c:594`). Exige patch de um caractere na cópia — feito e provado: com `[5]`, `exit 0`. Não é nota de portabilidade, é estouro de pilha que sempre esteve lá. |
| B2 | `mem_Init` (`memory_obj.c`) faz `memlog_fp = NULL` quando `WRITE_MEMORY_MAP=NO`, e depois há `fclose (memlog_fp)` sem guarda. | `SIGSEGV` em `_IO_new_fclose (fp=0x0)`. Contornado com `WRITE_MEMORY_MAP=YES` (§D4). Note-se que as escritas *usam* guarda (`if (memlog_fp)`), só o `fclose` não. |
| B3 | `landclass_LogIt` estoura buffer (pego por `__chk_fail`), alcançado com `LOGGING=YES` + `LOG_LANDCLASS_SUMMARY=yes`. | `SIGABRT`. Contornado com `LOGGING=NO` (§D4). Não investigado a fundo: está no caminho de landuse, que §C exclui. |
| B4 | `igrid_obj.c:1271`: `if ((this_year >= start_year) \| (i = scen_GetRoadDataFileCount () - 1))` — `\|` em vez de `\|\|`, e **`i =` em vez de `i ==`**. A atribuição vale 3 (verdadeiro, sempre) **e sobrescreve o índice do laço**, que termina na primeira iteração. Pior: `buf`/`this_year` já foram lidos de `i = 0`, mas o corpo relê `scen_GetRoadDataFilename (i)` com `i` já valendo 3. | Em `predict` carrega-se **uma** grade de estrada: o **arquivo do último ano** (`roads.1990.gif`), gravado com o **ano do primeiro** (1930). Confirmado instrumentando a cópia: `road_count=1`, `road[0]=demo200.roads.1990.gif`. O dado usado acaba sendo o razoável — por acidente, porque o último arquivo também é o mais recente. Se um cenário listasse as estradas em outra ordem, `predict` carregaria silenciosamente a grade errada. Para o harness o efeito é benigno e simplifica: uma grade de estrada (§D3). |
| B5 | `growth.c:213-216` chama `stats_SetSNG(sng)`, `stats_SetSDG(sdg)`, **`stats_SetSDG(sdc)`**, `stats_SetOG(og)`. **`stats_SetSDC` não existe** — só `stats_SetSDG` (`stats_obj.h:75`). E `sdg` é variável local que `growth.c` zera e `spr_spread` nunca escreve (não está na assinatura). | Logo: a coluna **`sdg`** do `avg.log` carrega o valor de **`sdc`**, e a coluna **`sdc`** é sempre 0. No `demo200` ambas dão 0,00 e o defeito fica invisível — é exatamente o tipo de coisa que produz uma falha falsa de V7 e come um dia de depuração. Está em §D5 e no primeiro item da lista de suspeitas de §V. O slot `sdg` é vestigial: das quatro regras do artigo, o código só reporta quatro contadores e este não é um deles. |
| B6 | O VS7 de S02 constrói o `grow` numa cópia em `mktemp -d` e **apaga no `trap EXIT`**. O `SLEUTH/grow` que existe na árvore é o ELF **MIPS N32 de 2001** que veio no tarball (346 700 bytes, sem bit de execução); `grow.exe` é de 2005. | O baseline de V7 era **efêmero** — o S02 provou que o original *compila*, não deixou nada com que comparar. T1 resolve com `build/sleuth-ref/`. Vale corrigir a leitura otimista da sessão 01 ("o baseline de V7 está garantido"): estava garantido que compila. |
| B7 | A entrada artificial do `main.c` usa um núcleo urbano de **4×4 fixo**, independente do tamanho da grade. Num 2000×2000 são 16 células urbanas em 4·10⁶, e a fração urbana final cai de 7,8 % (200×200) para 0,67 % (2000×2000) — §D8. | Um benchmark grande hoje mede varredura de grade vazia, não crescimento. E o aglomerado é **central**: o particionamento por faixas de linhas de S05 terá desequilíbrio de carga severo, com as faixas das pontas praticamente sem trabalho. T7 torna densidade e distribuição configuráveis — e a distribuição "aglomerado central" passa a ser um **caso de teste de desequilíbrio** proposital, útil para S08, em vez de um acidente. |
| B8 | `g_workspace` é alocado com `total_pixels * sizeof (PIXEL)` = `8 * total_pixels` bytes. O B7 corrigido precisa de `2 * total_pixels * sizeof (int)` = `8 * total_pixels`. **Cabe exatamente**, por a coincidência de `sizeof (PIXEL) == 2 * sizeof (int)` nesta plataforma. | Nenhum bug hoje, fragilidade amanhã: basta `PIXEL` virar `int`, ou `int` virar 64 bits, para a correção do B7 passar a estourar — silenciosamente, porque é a última grade alocada. T10 põe `_Static_assert`. |
| B9 | **Questão 4 da sessão 01 resolvida.** `run_value = (int)(roads[...] / MAX_ROAD_VALUE * diffusion)` é divisão inteira (`PIXEL` é `long`, `MAX_ROAD_VALUE` é `int` 100). Mas `igrid_NormalizeRoads` deixa toda célula de estrada valendo **exatamente 100** quando há uma só grade carregada (§D3), então `roads/100 == 1` e `run_value == diffusion`. | **A divisão inteira é mascarada pela normalização nos dados reais** — não é bug no `demo200`. Ela morde quando as grades de estrada de anos diferentes têm máximos diferentes: aí `norm_factor < 1` nos anos mais fracos, os valores caem abaixo de 100, `run_value` vira 0 e a fase 5 **desliga em silêncio** naquele ano. Bug latente e dependente dos dados, não erro de digitação. De quebra: o valor 100 escolhido para a estrada artificial do S02 era, por acidente, o único que reproduz o comportamento real. |
| B10 | **Questão 5 da sessão 01 resolvida: o `demo200` não dispara o B7.** `growth_count` conta as células com `delta > 0` (candidatas da fase 5), e o `grw_pix` do `avg.log` em 20 anos vai de **73 a 117**, sempre abaixo de `nrows = 200`. | Previsão da sessão 01 confirmada, e por medição em vez de geometria. Mas a margem é de **1,7×**, não de ordem de grandeza: horizonte de predição mais longo, coeficientes maiores ou grade menos quadrada cruzam o limite. Continua sendo o 5º suspeito de VS11, não o 1º. |
| B11 | **O `average_slope` que `spr_spread` devolve é saída morta no original.** `growth.c:204` passa `&average_slope` para `spr_spread` e depois **nenhum `stats_Set*` o consome** — as seis chamadas de `growth.c:213-218` são SNG, SDG, SDG, OG, RT, POP. A coluna `slope` do `avg.log` vem de outro lugar: `stats_circle` (`stats_obj.c:2653-2677`), que percorre a grade inteira e faz `addslope/number` sobre **todas** as células com `Z > 0`. | São dois números diferentes: média da declividade das células que **cresceram no ano** (o de `spr_spread`, descartado) contra média sobre a **área urbana inteira** (o publicado). Este spec mandava comparar o primeiro contra `$16` — estava errado, e a comparação falharia por um motivo que não é V7. Corrigido em §D5. O `sleuth-par` passou a imprimir os dois: `cresc_decl` (o de `spr_spread`, mantido porque é indicador útil de divergência para V3 em S07) e `decl_z` (`urban_mean_slope`, réplica de `stats_circle`), e é o segundo que entra na comparação. Terceira saída inútil do original catalogada, ao lado do `sdg` de §B5 e da macro `URBANIZE` morta (B8 do mestre). |
| B12 | **A re-estampagem anual da semente (T6, `growth.c:241-248`) é um no-op.** Medido, não deduzido: compilando com e sem a chamada, o `z` final é byte a byte idêntico em 20 anos de `demo200` e em 8 anos da entrada artificial, e as estatísticas não mudam. A razão é a guarda do merge — `if ((z[i] == 0) && (delta[i] > 0))` — que nunca reescreve célula já urbana; as células da semente valem `PHASE0G` desde a inicialização e ninguém as toca. | Implementada mesmo assim, por três motivos: fidelidade ao laço anual do original (o `sleuth-par` passa a fazer tudo o que `grw_grow` faz, sem exceção a explicar); custo de um passe por ano, fora do caminho quente; e porque *é* no-op **sob a regra de merge atual** — se S05/S06 mexerem no merge, deixa de ser, e aí a ausência seria uma divergência silenciosa. Corrige a afirmação de §D6, que dizia que a re-estampagem mudava `z` byte a byte. |
| B13 | **O `--dump` não escrevia o cabeçalho `nrows ncols`.** §T4/T3 deste spec e o `PROCEDENCIA.md` afirmavam que o formato do dump e o do `--load` eram o mesmo, "para que dump e entrada sejam intercambiáveis". Não eram: o `mkdata.sh` emite cabeçalho e o `dump_grid` do S02 não emitia. | Documentação descrevendo uma intenção como se fosse fato — ninguém tinha exercitado o caminho, porque até T4 não havia `--load` para consumir o dump. Corrigido no `dump_grid`, e a intercambialidade passou a ser exercida de fato: o dump de `z` de uma execução alimenta o `--load` de outra. Os dumps de VS6 e do check B7 mudaram de forma, mas as comparações são dump-contra-dump e seguem válidas. |
| B14 | **Arredondamento duplo produziu uma falha falsa de V7.** No ano 1997 o comparador acusou `decl_z` 2,41 (referência) contra 2,42 (protótipo). Os dois estavam certos: o valor verdadeiro é ≈2,4147; o protótipo imprimia com três casas ("2.415") e o `awk` relia essa string. E o **gawk 5.2 com MPFR arredonda `%.2f` de 2.415 para 2.42, enquanto o `printf` do C dá 2.41** — o primeiro arredonda o decimal, o segundo o binário. | Não é defeito de nenhum dos dois programas nem do gawk: é meu, por arredondar duas vezes. Dois consertos: o `decl_z` passou a sair com seis casas, e o comparador deixou de exigir igualdade de string na declividade, passando a exigir `\|dif\| <= 0,005` — que é literalmente a asserção "o valor do protótipo arredonda para o que o `avg.log` publica", e é o máximo que se pode afirmar contra uma saída de duas casas. As seis colunas inteiras continuam em igualdade exata. **Lição para S07 e S08:** toda comparação numérica contra saída formatada tem de ser feita na precisão da saída, nunca reformatando o lado preciso. **Reincidiu na T12**, o que promove isto de episódio a padrão: escrevi a VS10 comparando a string das colunas de coeficientes contra `2 20 5 51 5`, que é como §D4 as **cita**, enquanto o `avg.log` as **publica** como `2.00 20.00 5.00 51.00 5.00`. Falha falsa na primeira rodada, consertada normalizando com `%g` antes de comparar. Mesma raiz das duas vezes: tomar a forma abreviada do documento pela forma real da saída. |

---

## §N notas para o texto

**Para §4.1 (metodologia de verificação)** — este estágio dá o que o capítulo de
verificação precisa: a cadeia de evidência de que o protótipo serial é o SLEUTH.
O argumento forte é o de §D7 — o gerador de números aleatórios é bit a bit
idêntico e semeado no mesmo ponto —, porque sem ele nenhuma comparação numérica
teria significado. E o de §D5: comparar 7 estatísticas × 20 anos contra a saída
textual do original, sem instrumentá-lo.

**Para §3.1 (tabela de divergências)** — a tabela deixa de ser uma curiosidade
(B8 do mestre, a macro `URBANIZE` morta) e passa a ter peso. São agora sete
defeitos no código de referência, todos verificados e três deles **fatais em
toolchain moderno**: estouro de pilha em `date_str` (§B1), `fclose(NULL)` (§B2),
estouro em `landclass_LogIt` (§B3), atribuição no lugar de comparação com efeito
colateral no índice do laço (§B4), `stats_SetSDC` inexistente (§B5), o
`average_slope` calculado e descartado (§B11), e o aliasing de workspace já
conhecido (B7 do mestre). Vale a observação honesta de que isto **não
desqualifica** o SLEUTH — é software científico de 2001 que rodou em dezenas de
cidades e cujo modelo é a referência da área. Desqualifica a ideia de tratar um
código de referência como oráculo sem verificá-lo.

Há um subgrupo que rende parágrafo próprio: **três saídas que o código calcula
e joga fora** — o `sdg` que nunca é escrito (§B5), o `average_slope` que
`growth.c` recebe e ignora (§B11) e a macro `URBANIZE` morta (B8 do mestre).
Não são bugs de execução; são vestígios de refatorações que ninguém terminou.
Importam para este trabalho porque **é exatamente aí que a verificação
tropeça**: duas das três armadilhas de mapeamento de §D5 vêm desse subgrupo.

**Para §4.1, sobre o método** — §B14 dá o exemplo concreto de um erro de
medição que não é erro de programa: a única falha que a comparação de V7
acusou foi de arredondamento duplo meu, não de divergência entre os dois
códigos. Comparar contra saída formatada exige comparar **na precisão da
saída**. Vale como nota metodológica antes das tabelas de S07 e S08.

**Para §3.2 (exclusão mútua)** — §B9 acrescenta um item à taxonomia de B12:
além de `delta` (RMW), `ran_random` (estado estático) e `last_index` (estado
estático), há **acoplamento por pré-processamento** — o valor que a fase 5 lê
depende de uma normalização feita em outro objeto, muito antes, sobre o conjunto
inteiro das grades. Não é corrida de dados, mas é a mesma família de problema:
estado global compartilhado com dependência de ordem implícita.

**Para §4.2 (desempenho)** — §D8 e §B7 dão o enquadramento honesto: o baseline de
speedup é o próprio protótipo com 1 thread, o `grow` serve de oráculo de
corretude, e grades abaixo de 1000×1000 não sustentam afirmação de desempenho
(V15). E a distribuição da semente vira variável experimental: aglomerado central
mede desequilíbrio de carga, distribuição uniforme mede o teto do particionamento
por faixas.

**Alterações de `I.cli` no mestre** que este estágio exige (T4, T5, T8). As três
primeiras já estão implementadas; `--repeat` entra com T8:

```
sleuth-par [--threads N] [--years Y] [--rows R] [--cols C] [--seed S]
           [--variant A|B|C|none] [--dump FILE] [--hash]
           [--load DIR] [--coeff d,b,s,sr,rg] [--critical-slope V]
           [--repeat N]
```

Com `--load`, as dimensões vêm do cabeçalho dos arquivos e `--rows`/`--cols`
são ignorados com aviso. A linha que reproduz o cenário de referência é:

```
./sleuth-par --load data/demo200 --years 20 --seed 1 \
             --coeff 2,5,20,51,5 --critical-slope 15.0
```

Atenção à ordem de `--coeff`: é `difusão,breed,spread,slp_res,rd_grav`, a mesma
de `sim_SetCoefficients`. O `avg.log` imprime as mesmas cinco em **outra**
ordem — `diffus spread breed slp_res rd_grav` —, de modo que `2,5,20,51,5` na
linha de comando aparece como `2 20 5 51 5` na saída da referência.
