# S02 — base serial portável

**Estágio:** S02 · **Status:** pendente · **Dia no plano:** 16/09 (atrasado)
**Mestre:** `../SPEC.md` · **Cita:** V1, V2, V7, V9, V14
**Alimenta o texto:** §3.1 (Núcleo do código) — descrição geral do SLEUTH e do `spread.c`

---

## §G objetivo do estágio

Ter `spread-simplified.c` **compilando e rodando**, em C, serial, sobre matrizes
globais, numa grade 20×20, por N anos, sem threads e sem mudar o RNG.

Este estágio não paraleliza nada. Ele existe para que exista algo correto e
mensurável antes de introduzir concorrência — e porque V7 (serial do protótipo ==
SLEUTH original) é a fundação de todos os testes posteriores.

## §S escopo

**Entra:** árvore `sleuth-par/`, `globals.h`, `spread.c` adaptado, `stubs.c`,
`main.c`, `Makefile`, entrada artificial fixa 20×20, binário `grow` do original
compilado.

**Não entra:** threads (S05/S06), RNG ancorado à célula (S04), `testgen.c` e
comparação automatizada contra o baseline (S03), qualquer medição de tempo (S03).

## §D entradas conhecidas

Levantamento já feito sobre o código real — não precisa ser redescoberto.

### D1 — os 25 símbolos externos que `spread-simplified.c` consome

```
coeff_GetCurrentBreed        igrid_GetNumCols             stats_IncrementDeltaFailure
coeff_GetCurrentDiffusion    igrid_GetNumRows             stats_IncrementEcludedFailure
coeff_GetCurrentRoadGravity  igrid_GetRoadGridPtrByYear   stats_IncrementSlopeFailure
coeff_GetCurrentSlopeResist  igrid_GetSlopeGridPtr        stats_IncrementUrbanSuccess
coeff_GetCurrentSpread       igrid_GridRelease            stats_IncrementZFailure
igrid_GetExcludedGridPtr     mem_GetTotalPixels           util_condition_gif
proc_GetCurrentYear          mem_GetWGridFree             util_count_neighbors
scen_GetCriticalSlope        mem_GetWGridPtr              util_count_pixels
                                                          util_get_next_neighbor
                                                          util_init_grid
```

A lista de §6.3 do plano original **esqueceu dois**: `coeff_GetCurrentSlopeResist`
e `scen_GetCriticalSlope` (ambos usados em `spr_get_slp_weights`).

### D2 — `spread-simplified.c` não compila como está

Três protótipos perderam o `static` que existe no original (`SLEUTH/spread.c:86,
93, 105`), enquanto as definições continuam `static`:

| símbolo | protótipo em simplified | definição em simplified |
|---|---|---|
| `spr_road_walk` | linha 59, sem `static` | linha 561, `static` |
| `spr_urbanize_nghbr` | linha 66, sem `static` | linha 523, `static` |
| `spr_get_neighbor` | linha 78, sem `static` | linha 495, `static` |

Em C isso é erro: *static declaration follows non-static declaration*. Além
disso o arquivo usa `bool`, que não existe em `ugm_defines.h` (lá é `BOOLEAN`).

**Este é o primeiro obstáculo do estágio e ele é trivial** — a estimativa de
risco alto do plano para o dia 16 estava superdimensionada, sobretudo depois de
B9 (temos a árvore original inteira).

### D3 — de onde vem cada macro

O plano supunha escrever `globals.h` do zero. Não é o caso:

| item | origem real |
|---|---|
| `PIXEL` (`long`), `GRID_P`, `COEFF_TYPE`, `TRUE`/`FALSE`, `PHASE*G`, `MIN_NGHBR_TO_SPREAD`, `MAX_ROAD_VALUE`, `LT`…`GT` | `SLEUTH/ugm_defines.h` — aproveitar quase inteiro |
| `OFFSET`, `IMAGE_PT`, `INTERIOR_PT`, `MAX`, `MIN` | `SLEUTH/ugm_macros.h` — extrair só estes; o resto é logging/MPI |
| `RANDOM_ROW/COL/INT/FLOAT`, `ran_random`, `InitRandom` | `SLEUTH/random.h` + `random.c` — copiar como está neste estágio |
| `bool` | `<stdbool.h>`, mantendo `TRUE`/`FALSE` |

O `globals.h` original é só variáveis de MPI; não serve.

## §T tarefas

| id | st | tarefa | cita |
|----|----|--------|------|
| T1 | x | Criar `sleuth-par/` e `Makefile`: `gcc -std=c11 -Wall -Wextra -pthread -UNDEBUG -g`. Alvo serial primeiro; `-O2` só depois de T10 passar. | V1 |
| T2 | x | `globals.h`: extrair de `ugm_defines.h`/`ugm_macros.h` apenas o necessário (ver D3). `OFFSET(i,j)` passa a usar a global `g_ncols`, **sem chamada de função**. | V14 |
| T3 | x | Consertar D2: `static` nos protótipos, `#include <stdbool.h>`. Compilar limpo em `-Wall -Wextra`. | — |
| T4 | x | Declarar as matrizes globais `g_z`, `g_delta`, `g_slp`, `g_excld`, `g_roads` e os escalares `g_nrows`, `g_ncols`, `g_total_pixels`. Alocação única na inicialização. | V9 |
| T5 | x | `stubs.c`: os símbolos de D1. `igrid_*`/`mem_*` viram retorno de constante; `coeff_*`/`scen_*`/`proc_*` leem globais setadas pelo `main`; `stats_Increment*` são contadores simples (a versão thread-safe é S06). **Seis símbolos foram eliminados, não implementados — ver BS6.** | V5 |
| T6 | x | Portar `util_init_grid`, `util_count_neighbors`, `util_get_next_neighbor`, `util_condition_gif`, `util_count_pixels` de `SLEUTH/utilities.c`, removendo `FUNC_INIT`/`FUNC_END`/logging. Comportamento idêntico. | V7 |
| T7 | x | Corrigir B7: deslocamento de `growth_col` passa de `nrows` para `total_pixels`. Comentado no código, citando o original. **Bug demonstrado empiricamente — ver BS5.** | V12 |
| T8 | x | `main.c`: alocar e inicializar as matrizes, setar coeficientes, laço `for ano = 1..Y` chamando `spr_spread`, imprimir por ano `sng/sdc/og/rt/num_growth_pix/average_slope/pop`. | — |
| T9 | x | Entrada artificial 20×20 embutida: `z` com um núcleo urbano central, `slp` com gradiente, `excld` zerado exceto uma faixa, `roads` com uma via reta. Determinística, sem arquivo. `testgen.c` fica para S03. | — |
| T10 | x | Rodar 5 anos em 20×20 com asserts ligados: sem crash, sem leitura fora de faixa (`valgrind --error-exitcode=1`), contadores plausíveis (crescimento > 0, `pop` monotônico não-decrescente). | V2, V9 |
| T11 | x | Compilar o SLEUTH original: `make -C SLEUTH`. Guardar `grow` como baseline para S03. | I.baseline |

## §V verificação do estágio

Todas devem passar antes de S03:

- **VS1** — `make` limpo, zero warnings em `-Wall -Wextra`.
- **VS2** — 5 anos em 20×20 rodam até o fim, `valgrind` sem erro.
- **VS3** — `z` não é escrito fora do laço de merge. Verificável por inspeção:
  nenhuma das três fases nem `spr_urbanize` referencia `g_z` em lvalue. (V2)
- **VS4** — Nenhuma chamada a `malloc`/`calloc` depois da inicialização. (V9)
- **VS5** — `OFFSET` não chama função. Confere com `grep`. (V14)
- **VS6** — Duas execuções com a mesma semente dão saída idêntica. É mais fraco
  que V3, mas é o pré-requisito dela.
- **VS7** — `SLEUTH/grow` compila e executa ao menos um cenário de exemplo.

## §B achados do estágio

| id | data | achado | consequência |
|----|------|--------|--------------|
| BS1 | 18/09 | §D2 subestimou: são **quatro** protótipos sem `static`, não três. Faltava `spr_road_search` (`spread-simplified.c:52` declara sem, `:620` define com). | Corrigido em T3. Lição: a tabela de §D2 foi montada por inspeção visual; o compilador é a autoridade. Rodar o compilador antes de catalogar. |
| BS2 | 18/09 | Além de §D2, `spread-simplified.c` tem mais dois defeitos de compilação: (a) usa `MAX_SLOPE_RESISTANCE_VALUE` em `spr_get_slp_weights`, constante que ficou para trás em `ugm_defines.h`; (b) `spr_spread` usa `func` em `__FILE__, func, __LINE__` mas perdeu a declaração `char func[] = "spr_spread"`. | Ambos corrigidos em T3. A constante entrou em `globals.h`; a declaração foi restaurada com comentário citando a origem. Reforça B10 do mestre. |
| BS3 | 18/09 | `util_get_next_neighbor` (`SLEUTH/utilities.c:602`) guarda `static int last_index` — a sequência de vizinhos é **estado global mutável entre chamadas**. É consumida por `spr_get_neighbor` e `spr_road_walk`. | **Segundo ponto não reentrante do núcleo**, além do `ran_random` (B5 do mestre). Com threads vira corrida de dados *e* quebra de determinismo, e não está coberto pela região crítica de B3 do plano (o RMW em `delta`). Precisa de tratamento próprio em S05/S06 — provavelmente estado por thread. Promovido a §B12 do mestre. |
| BS4 | 18/09 | Erro meu, não do código: escrevi `mem_*/igrid_*` dentro de um comentário `/* */`; o `*/` fechou o comentário no meio. | Trivial, corrigido. Registrado porque o modo de falha (comentário que engole código) produz erros de sintaxe a dezenas de linhas de distância da causa. |
| BS5 | 18/09 | **B7 foi demonstrado, não só alegado.** `spread.c` ganhou a flag `-DSLEUTH_ORIGINAL_B7_BUG`, que restaura o deslocamento original. Comparando as duas saídas: numa grade 60×60 elas são **idênticas** — o bug não dispara; numa grade **8×600** elas **divergem**. | A condição de disparo é `growth_count > nrows`, e `growth_count` cresce com `total_pixels`. Logo o gatilho é a razão colunas/linhas, não o tamanho absoluto: grades quadradas escondem o bug, grades largas e baixas o expõem. **Consequência para S03:** o `demo200` é 200×200, quadrado — provavelmente **não** dispara o bug, então a comparação de V7 contra o `grow` original deve fechar mesmo com a correção aplicada. Se não fechar, este é o primeiro suspeito. O teste está em `tests/run_checks.sh`. |
| BS6 | 18/09 | A globalização não transformou os 25 símbolos de §D1 em 25 stubs: **eliminou 6**. `igrid_GetExcludedGridPtr`, `igrid_GetRoadGridPtrByYear`, `igrid_GetSlopeGridPtr`, `igrid_GridRelease`, `mem_GetWGridPtr` e `mem_GetWGridFree` existiam só para emprestar e devolver ponteiros de grade. Restam 19. | Resultado melhor do que o previsto, e com argumento próprio para §3.2 do texto: o pool de grades do `memory_obj` é estado global mutável com contador de empréstimos — sob N threads ele precisaria de trava própria. A globalização **remove** esse ponto de sincronização em vez de sincronizá-lo. Um problema de concorrência a menos, antes mesmo de existir thread. |

## §N notas para o texto

O que este estágio produz para §3.1 do TCC:

- Modelo de memória do SLEUTH: matrizes achatadas em 1-D, `PIXEL = long`, macro
  `OFFSET` fazendo a indexação linha-major — e o custo escondido de ela chamar
  `igrid_GetNumCols()` a cada acesso (B3).
- `spr_spread` como dispatcher: coeficientes → zera `delta` → tabela de pesos de
  declividade → três fases → dois filtros `util_condition_gif` → merge.
- O mapeamento **quatro regras do artigo → três funções** e a tabela de
  divergências (§5 do plano, mais B7 e B8 do mestre).
- Os stubs como recorte deliberado: o que foi removido (calibração, GIF,
  deltatron, interpolação) e por quê.
