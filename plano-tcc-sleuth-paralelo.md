# Plano de Trabalho — TCC: Paralelização do núcleo do SLEUTH com threads POSIX

**Autor:** Gustavo Farias Dropa
**Orientador:** Prof. Koscianski
**Data do documento:** 14/09/2026
**Prazo da entrega ao orientador:** 25/09/2026

---

## 1. Escopo e entregáveis

O trabalho tem dois produtos, que caminham em paralelo ao longo das duas semanas:

1. **Protótipo funcional em C**, que reimplementa o núcleo de simulação do SLEUTH (`spread`) com processamento multicore usando threads POSIX, tratando explicitamente os problemas de exclusão mútua que a paralelização introduz.
2. **Texto do TCC**, seguindo o esqueleto de seções enviado pelo orientador.

### 1.1 Objetivo (reformulado conforme o esqueleto)

Desenvolver um protótipo de simulador de crescimento urbano equivalente ao SLEUTH, com processamento multicore, tratando os problemas de exclusão mútua decorrentes do acesso concorrente às matrizes de dados compartilhadas.

Observação sobre o esqueleto: a seção de justificativa foi suprimida. A seção 1 termina no objetivo.

### 1.2 O que **não** está no escopo

- Calibração (brute force, Monte Carlo, métricas de ajuste tipo Lee-Sallee).
- Leitura/escrita de imagens GIF.
- Interpolação temporal entre anos históricos.
- O modelo Deltatron de cobertura do solo.
- Validação com dados geográficos reais.

Tudo isso existe no SLEUTH completo, mas fica de fora: o `spread-simplified.c` já vem sem essas partes, e o foco do TCC é o desempenho do núcleo, não a qualidade da previsão urbana.

---

## 2. Material recebido do orientador

| Peça | O que é | Papel no trabalho |
|---|---|---|
| Esqueleto de seções | Estrutura do texto, seção por seção | Define o sumário do TCC |
| `spread-simplified.c` (873 linhas) | Núcleo do SLEUTH, sem I/O, sem estatísticas de calibração, sem interpolação | É o código a ser adaptado |
| Clarke, Hoppen & Gaydos (1997) | Artigo seminal do modelo, em *Environment and Planning B* 24(2):247–261 | Referência principal; a Figura 4 descreve as quatro regras de crescimento |

O orientador observou que **existem diferenças entre o código e a figura do artigo**, e espera que essas diferenças sejam identificadas e documentadas. Ver seção 4 deste plano.

---

## 3. Decisões técnicas fechadas

### 3.1 Linguagem: C, não C++

O código permanece em C. O porte para C++ foi considerado e descartado.

**Motivos:**

- O `spread-simplified.c` já é C. O porte teria atrito real, não cosmético. O caso mais concreto: o `globals.h` do SLEUTH define `bool` por `typedef`/`#define`, o que é ilegal em C++, onde `bool` é palavra reservada.
- Pthreads cobre integralmente as necessidades do trabalho (ver tabela abaixo).
- Ganho pedagógico: `pthread_mutex_lock` e `sem_wait` são a tradução direta das operações P/V de Dijkstra, que é o vocabulário da seção 2.2. O `std::mutex` seria uma camada de abstração a mais para explicar.
- Comparabilidade: mantendo C, o diff entre o SLEUTH original e a versão paralela é curto e legível, e pode ir para um apêndice como evidência direta da contribuição. Reescrito em C++, o trabalho viraria "um simulador inspirado no SLEUTH", que é um argumento mais fraco.
- O orientador foi explícito: "precisamos de código C".

| Necessidade | Recurso em C |
|---|---|
| Disparar N threads | `pthread_create` / `pthread_join` |
| Exclusão mútua | `pthread_mutex_t` |
| Semáforo | `sem_t` (`<semaphore.h>`) |
| Barreira entre fases/anos | `pthread_barrier_t` |
| CAS atômico sobre o `delta` | `__atomic_compare_exchange_n` (builtin GCC) ou `<stdatomic.h>` |

Compilação com `-pthread`.

### 3.2 Modelo de threads

Threads de kernel (NPTL, mapeamento 1:1, sincronização via futex). N threads disparadas a partir de `spr_spread`, particionamento estático.

### 3.3 Matrizes globais

Conforme a orientação do professor, as matrizes de dados (`z`, `delta`, `slp`, `excld`, `roads`) passam a ser globais, e não parâmetros passados função a função. Isso simplifica a assinatura das funções executadas pelas threads.

---

## 4. Análise do código base

### 4.1 Estrutura de `spread-simplified.c`

A função que desencadeia tudo é `spr_spread()`, no final do arquivo. Ela:

1. Obtém os coeficientes correntes (`road_gravity`, `diffusion`, `breed`, `spread`).
2. Zera a matriz `delta` (`util_init_grid`).
3. Monta a tabela de pesos de declividade (`spr_get_slp_weights`).
4. Chama as três fases em sequência: `spr_phase1n3`, `spr_phase4`, `spr_phase5`.
5. Aplica filtros (`util_condition_gif`) sobre o `delta`.
6. Faz o **merge**: percorre todos os pixels e, onde `z[i] == 0 && delta[i] > 0`, escreve `z[i] = delta[i]`, acumulando `num_growth_pix` e `average_slope`.

As quatro regras de crescimento do artigo mapeiam para **três** funções:

| Regra no artigo (Figura 4) | Função no código |
|---|---|
| Spontaneous growth | `spr_phase1n3` (primeira metade) |
| Diffusive growth / new spreading center | `spr_phase1n3` (segunda metade, sob `breed_coefficient`) |
| Organic growth | `spr_phase4` |
| Road influenced growth | `spr_phase5` |

Funções auxiliares: `spr_urbanize` (a urbanização propriamente dita), `spr_urbanize_nghbr`, `spr_get_neighbor`, `spr_road_search`, `spr_road_walk`, `spr_spiral`, `spr_GetDiffusionValue`, `spr_GetRoadGravValue`.

### 4.2 Achado 1: o double buffering já existe

As três fases **leem de `z` e escrevem exclusivamente em `delta`**. O `z` só é modificado no laço de merge, ao final de `spr_spread`.

Isso é a descoberta mais importante da análise. O padrão clássico de paralelização segura de autômatos celulares — ler de um buffer "atual" e escrever num buffer "próximo" — **já está implementado no SLEUTH original**. Não é preciso criar a matriz de saída: `delta` é o buffer "next" e `z` é o "current".

Consequência prática: o particionamento por faixas de linhas não gera corrida de leitura. Cada thread pode ler qualquer célula de `z` livremente, inclusive as linhas de fronteira das faixas vizinhas, porque `z` é imutável durante as fases.

### 4.3 Achado 2: a região crítica é uma linha só

Toda escrita de crescimento passa por `spr_urbanize`. O trecho relevante:

```c
if (z[OFFSET(row,col)] == 0) {
  if (delta[OFFSET(row,col)] == 0) {         /* lê   */
    if (RANDOM_FLOAT > swght[slp[OFFSET(row,col)]]) {
      if (excld[OFFSET(row,col)] < RANDOM_INT(100)) {
        val = TRUE;
        delta[OFFSET(row,col)] = pixel_value; /* escreve */
        (*stat)++;
        stats_IncrementUrbanSuccess();
```

O par "testa se `delta` é zero, depois escreve" é um **read-modify-write** clássico. Duas threads podem observar `0` simultaneamente, ambas passarem nos testes e ambas escreverem, contabilizando **dois** crescimentos onde houve apenas um.

Efeito colateral do erro: o valor final de `delta` é quase inofensivo (as duas threads escrevem um `pixel_value`, possivelmente com tags de fase diferentes), mas os contadores `sng`, `sdc`, `og`, `rt` e os contadores globais de `stats_Increment*` ficam corrompidos. Como esses contadores alimentam as regras de automodificação do SLEUTH, o erro se propaga para os anos seguintes.

**Essa é a seção crítica do trabalho, e ela é pequena** — o que é ótimo, porque permite implementar e comparar três estratégias diferentes de exclusão mútua sem reescrever o modelo.

### 4.4 Achado 3: cada fase tem um padrão de paralelismo diferente

| Fase | Padrão de acesso | Estratégia de paralelização |
|---|---|---|
| `spr_phase1n3` | Sorteia `1 + diffusion_value` células aleatórias em **todo** o grid (`RANDOM_ROW`, `RANDOM_COL`). Não varre a matriz. | Não é particionável espacialmente. Divide-se o **laço de iterações** entre as threads. Toda escrita é não-local, exige exclusão mútua. |
| `spr_phase4` | Duplo laço `for row / for col` sobre o interior da matriz. Lê `z` (read-only) e urbaniza uma vizinha sorteada. | **Faixas de linhas.** É a única fase verdadeiramente data-parallel sobre a grade. Sem lock, exceto pelo RMW de 4.3. |
| `spr_phase5` | Varre `total_pixels` para montar a lista de crescimento, depois faz `1 + breed` "road trips" que caminham pela malha viária e escrevem **longe** da origem. | Pior caso. As escritas são arbitrárias em posição. É aqui que a exclusão mútua realmente pesa. |

Uma ressalva sobre o enunciado do esqueleto ("o miolo contém 5 laços aninhados que devem percorrer toda a matriz"): isso descreve o **aninhamento do SLEUTH completo** — conjuntos de coeficientes → iterações de Monte Carlo → anos → linhas → colunas. Dentro do `spread-simplified.c`, apenas `spr_phase4` faz varredura completa da grade; `spr_phase1n3` é O(diffusion) sorteios e `spr_phase5` é uma varredura mais O(breed) caminhadas. Isso precisa estar redigido com precisão na seção 1 e retomado na 3.1.

### 4.5 Achado 4: o RNG global impede o determinismo

`RANDOM_INT`, `RANDOM_FLOAT`, `RANDOM_ROW` e `RANDOM_COL` são macros sobre estado global (no SLEUTH original, `rand()` semeado pelo PID do processo).

Com múltiplas threads, a ordem de consumo dos números aleatórios muda a cada execução. O teste exigido pelo esqueleto — "testes com paralelização (2 threads), verificar se dados batem com 1 thread" — torna-se impossível de passar.

**Solução adotada:** ancorar o sorteio **à célula, não à thread**. Cada célula recebe sua sequência de números a partir de uma chave determinística, tipicamente `(semente_global, ano, i, j)` mais um contador de subpasso, usando um gerador contador-based (Philox / Random123). Assim, a mesma célula recebe os mesmos números em qualquer configuração de threads.

Alternativa mais simples, se o tempo apertar: um estado de RNG por thread, aceitando que a saída de 1 e 2 threads seja apenas **estatisticamente equivalente** e não idêntica. Isso enfraquece o capítulo 4 e só deve ser usado como último recurso.

### 4.6 Achado 5: as grades pequenas não mostrarão speedup

Com matrizes 20×20 e 100×100, o custo de criação de threads e a contenção nos locks dominam o tempo de execução. Não haverá ganho, e possivelmente haverá perda.

**Implicação para o capítulo 4:** as grades pequenas servem exclusivamente para **corretude**. Para as medições de tempo será necessário chegar a 1000×1000 ou mais, e/ou simular muitos anos por medição. Isso precisa ser previsto no cronograma (dia 23) e declarado honestamente no texto.

---

## 5. Divergências entre o código e o artigo

Documentar na seção 3.1 do TCC.

| Regra | Artigo (Figura 4) | Código |
|---|---|---|
| Organic growth | "for all cells with at least **three** neighbors" | `spr_phase4`: `urb_count >= 2 && urb_count < 8` — ou seja, **dois** vizinhos bastam, e células totalmente cercadas são excluídas |
| Spontaneous growth | "if this location has at least one urban neighbor **or** passes a randomized test of slope suitability" | `spr_phase1n3`: sorteia uma célula qualquer e testa apenas declividade e exclusão; não há verificação de vizinhança urbana |
| Estrutura das regras | Quatro regras apresentadas separadamente | Três funções; espontâneo e difusivo estão fundidos em `spr_phase1n3` |

Interpretação sugerida para o texto: o artigo de 1997 descreve o modelo conceitual, enquanto o código reflete ajustes feitos durante a calibração e a evolução posterior do software. A discrepância não é erro de nenhum dos dois; é a distância normal entre especificação publicada e implementação em produção. **O código é a referência normativa para este trabalho.**

---

## 6. Arquitetura do protótipo

### 6.1 Arquivos

```
globals.h        macros e tipos que faltam (ver 6.2)
spread.c         o núcleo adaptado, a partir do spread-simplified.c
stubs.c          implementações mínimas das funções externas (ver 6.3)
rng.c / rng.h    gerador contador-based, ancorado à célula
main.c           laço de anos e orquestração
testgen.c        geração de dados artificiais
Makefile
```

### 6.2 O que precisa entrar no `globals.h`

O `spread-simplified.c` faz `#include "globals.h"`, que não foi enviado. É preciso escrevê-lo:

- Tipos: `GRID_P`, `PIXEL`, `COEFF_TYPE`, `bool` / `TRUE` / `FALSE`
- Macro `OFFSET(i,j)` → indexação linha-major sobre array 1-D: `((i) * ncols + (j))`
- Macros `INTERIOR_PT(i,j)`, `IMAGE_PT(i,j)`
- Constantes `PHASE0G`, `PHASE1G`, `PHASE3G`, `PHASE4G`, `PHASE5G`
- `MIN_NGHBR_TO_SPREAD`, `MAX_ROAD_VALUE`, `MAX(a,b)`
- Macros de aleatoriedade `RANDOM_INT`, `RANDOM_FLOAT`, `RANDOM_ROW`, `RANDOM_COL` (redirecionadas para o novo RNG)
- Comparadores `GT`, `GE`

### 6.3 Funções externas a implementar como stubs

Chamadas pelo `spread-simplified.c` mas definidas em outros arquivos do SLEUTH:

- `igrid_GetNumRows`, `igrid_GetNumCols`
- `igrid_GetExcludedGridPtr`, `igrid_GetRoadGridPtrByYear`, `igrid_GetSlopeGridPtr`, `igrid_GridRelease`
- `mem_GetWGridPtr`, `mem_GetWGridFree`, `mem_GetTotalPixels`
- `util_init_grid`, `util_count_neighbors`, `util_get_next_neighbor`, `util_condition_gif`, `util_count_pixels`
- `stats_IncrementUrbanSuccess`, `stats_IncrementEcludedFailure`, `stats_IncrementSlopeFailure`, `stats_IncrementDeltaFailure`, `stats_IncrementZFailure`
- `coeff_GetCurrentRoadGravity`, `coeff_GetCurrentDiffusion`, `coeff_GetCurrentBreed`, `coeff_GetCurrentSpread`
- `proc_GetCurrentYear`

Com matrizes globais, a maioria dos `igrid_*` e `mem_*` vira retorno de ponteiro global ou de constante. Os `stats_Increment*` precisam ser thread-safe (contadores atômicos ou por-thread com redução no final).

### 6.4 `main.c`

Conforme o esqueleto:

```
carregar/gerar matrizes iniciais (z, slp, excld, roads)
inicializar N threads e estruturas de sincronização
para cada ano de 1 a N:
    spr_spread(...)              /* z é lido, delta é escrito, merge ao final */
    (a saída vira a entrada do ano seguinte, já que o merge escreve em z)
    coletar estatísticas do ano
escrever a matriz final / hash
```

Como o merge de `spr_spread` já escreve em `z`, a realimentação "saída vira nova entrada" é automática. Não é preciso fazer swap explícito de ponteiros no `main`.

Parâmetros de linha de comando: número de threads, número de anos, dimensões da grade, semente, variante de exclusão mútua.

### 6.5 Estratégia de sincronização

- **Barreira** (`pthread_barrier_t`) ao fim de cada fase. As fases são sequenciais entre si: `phase1n3` → barreira → `phase4` → barreira → `phase5` → barreira → merge.
- **Merge** paralelizável por faixas, com redução dos acumuladores `num_growth_pix` e `average_slope`.
- **Região crítica** no `spr_urbanize`: três variantes a implementar e comparar.

---

## 7. As três variantes de exclusão mútua

O experimento central do capítulo 4. Todas atacam o mesmo ponto — o read-modify-write sobre `delta` em `spr_urbanize`.

### Variante A — mutex global

Um único `pthread_mutex_t` protegendo todo o bloco de teste-e-escrita.

- Correta e trivial de explicar.
- Serializa todas as urbanizações; espera-se que seja o gargalo dominante com muitas threads.
- É a linha de base didática, e liga diretamente ao conceito de região crítica de Dijkstra.

### Variante B — sharding de locks

Um array de mutexes, um por faixa de linhas (ou por bloco). A thread trava apenas o mutex correspondente à região onde vai escrever.

- Reduz drasticamente a contenção nas fases locais.
- Continua tendo contenção em `spr_phase5`, onde as escritas são distantes.
- Permite discutir o trade-off entre granularidade de lock e overhead de memória.

### Variante C — compare-and-swap atômico

Substitui o lock por uma reivindicação atômica da célula. Não exige mudar o tipo `GRID_P` nem as assinaturas:

```c
unsigned char expected = 0;
if (__atomic_compare_exchange_n(&delta[OFFSET(row,col)], &expected,
                                pixel_value, 0,
                                __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    /* esta thread reivindicou a célula; contabiliza o crescimento */
}
```

- Lock-free, sem bloqueio de thread.
- Exige atenção ao *false sharing* (células vizinhas na mesma linha de cache).
- É o argumento mais forte do capítulo de desempenho.

**Nota sobre os contadores de estatística:** em todas as variantes, `(*stat)++` e os `stats_Increment*` também precisam de tratamento — acumuladores por thread com redução na barreira é o caminho mais barato e mantém o determinismo.

**Plano de corte:** se o cronograma apertar, implementar apenas a variante A e discutir B e C como trabalho futuro.

---

## 8. Plano de testes

### 8.1 Etapa 1 — corretude serial (1 thread, 20×20)

Dados artificiais gerados por `testgen.c`. Semente fixa. Compara-se com a execução do código original não modificado.

**Critério:** saída idêntica, bit a bit.
**Se falhar:** o erro está na portagem/stubs, não na concorrência.

### 8.2 Etapa 2 — determinismo (1 vs 2 vs 4 threads, 20×20 e 100×100)

**Critério:** a saída com N threads é idêntica à saída com 1 thread, para todo N.
**Se divergir, os suspeitos, em ordem:**
1. O RNG não está ancorado à célula (ver 4.5).
2. A ordem de aplicação das escritas de `spr_phase5` varia entre execuções.
3. Contadores de estatística sem proteção.

Instrumentação: hash (por exemplo, checksum simples) da matriz `z` ao final de cada ano, mais dump completo para comparação binária quando houver divergência.

### 8.3 Etapa 3 — desempenho

Grades: 100×100, 500×500, 1000×1000 (e maior, se necessário — ver 4.6).
Threads: 1, 2, 4, 8.
Variantes de exclusão mútua: A, B, C.

Medição com `clock_gettime(CLOCK_MONOTONIC)`, excluindo I/O e geração de dados da região cronometrada. Múltiplas repetições, reportando mediana.

**Métricas:**
- Speedup: S(N) = T(1) / T(N)
- Eficiência: E(N) = S(N) / N
- Fração serial estimada por Amdahl: S = 1 / ((1−p) + p/N)
- Escalabilidade forte (grade fixa) e fraca (grade cresce com N)

**Critério de decisão:** se a eficiência cair abaixo de 0,5 com 4 threads na variante A, isso confirma que a contenção no lock global é o gargalo e justifica as variantes B e C — que é exatamente a narrativa que o capítulo quer contar.

### 8.4 Teste preliminar — Jogo da Vida

Antes de tocar no SLEUTH, implementar o Jogo da Vida de Conway em C com a mesma arquitetura (double buffer, faixas de linhas, `pthread_barrier_t`). Serve para dois propósitos:

1. Validar a arquitetura de threads num autômato celular mínimo (2 estados, vizinhança de Moore, atualização síncrona), sem a complexidade estocástica e as escritas não-locais do SLEUTH.
2. Alimentar a seção 3.0 do TCC como introdução didática ao assunto.

Validação com padrões conhecidos (glider, blinker, block).

---

## 9. Mapeamento esqueleto → conteúdo → bibliografia

### Seção 1 — Introdução

Mantém o texto atual até "...os AC se tornaram uma ferramenta muito utilizada em estudos demográficos e urbanos [4]." Depois, acrescentar:

| Ponto a escrever | Fonte |
|---|---|
| Introduzir o software SLEUTH | Clarke, Hoppen & Gaydos (1997); Chaudhuri & Clarke (2013) |
| Custo computacional, laços aninhados varrendo a matriz | Silva & Clarke (2002) — frase literal sobre "a set of nested loops"; Dietzel & Clarke (2006) sobre o gargalo computacional |
| Ausência de multicore na época de criação | Clarke (2008), sobre a reescrita para MPI em Cray da EPA; cronologia da NPTL (2003) |
| Necessidade de tratar sincronia para usar multicore | Tanenbaum; Silberschatz; Dijkstra (1968) |

**§1.1 Objetivo:** conforme a seção 1.1 deste plano. Justificativa suprimida.

### Seção 2.1 — Autômatos Celulares

O que são (von Neumann 1966; Ulam); exemplos de aplicação (Game of Life — Gardner 1970; Wolfram 1983); características e funcionamento (vizinhanças de von Neumann e Moore, regras de transição — Scholarpedia); aplicações urbanas (White & Engelen 1993; Batty, Couclelis & Eichen 1997; Torrens & O'Sullivan 2001); e o **paralelismo inerente** — atualização síncrona e homogênea, dependendo apenas do estado anterior da vizinhança (Chopard & Droz).

### Seção 2.2 — Sincronia de Processos

O que é sincronia (Tanenbaum; Silberschatz); exemplo do incremento concorrente de variável global — `counter++` como read-modify-write em três passos; região crítica e exclusão mútua (Dijkstra 1968, EWD123; algoritmo de Dekker); mutexes e semáforos (operações P/V); e o **exemplo específico em AC** — que é justamente o achado 4.3 deste plano, o read-modify-write sobre `delta` no `spr_urbanize`. Usar o código real do SLEUTH como exemplo aqui é muito mais forte do que um exemplo genérico.

### Seção 2.3 — SLEUTH

O que é (acrônimo: Slope, Land use, Exclusion, Urban extent, Transportation, Hillshade); quando e por quem (Keith C. Clarke, então Hunter College/CUNY, com L. Gaydos do USGS e S. Hoppen; Project Gigalopolis, NCGIA/UCSB + USGS); panorama de uso (mais de 60 cidades segundo Chaudhuri & Clarke 2013; mais de 100 segundo Clarke 2008 — citar as duas cifras com suas fontes); compilação para Open MPI e o que é MPI em uma ou duas frases; visão geral da lógica com as quatro regras de crescimento e os cinco coeficientes (diffusion, breed, spread, slope resistance, road gravity), mais automodificação.

Reproduzir o trecho original do artigo sobre as quatro regras, conforme o esqueleto indica.

### Seção 3.0 — Teste inicial com o Jogo da Vida

Ver 8.4.

### Seção 3.1 — Núcleo do código

Descrição geral do SLEUTH (arquivos, modelo de memória flat, macro `OFFSET`, tipo `GRID_P`) e descrição detalhada do `spread.c`: `spr_spread` como dispatcher, as três fases, o merge final, e a tabela de divergências da seção 5 deste plano.

### Seção 3.2 — Paralelização

Threads de kernel (NPTL, modelo 1:1, futex — Drepper & Molnar 2003); o problema de sincronia encontrado (achados 4.2 a 4.5); e a forma de solução (double buffering já existente, barreiras, as três variantes de exclusão mútua, RNG ancorado à célula).

### Seção 4 — Testes

Ver seção 8 deste plano.

### Seção 5 — Conclusões

Incluir a **lacuna de literatura** como argumento de contribuição: não há trabalho publicado que paralelize o núcleo do SLEUTH com threads em memória compartilhada. As paralelizações existentes são de memória distribuída (pSLEUTH/pRPL — Guan & Clarke 2010; SLEUTH-3r — Jantz et al. 2010) ou de paralelismo de tarefas na calibração (DSLEUTH — Chaudhuri & Foley 2019). Enquadrar com precisão: a novidade é *SLEUTH + threads + exclusão mútua no núcleo*, não "paralelizar AC com threads" em geral, que é assunto vasto.

---

## 10. Cronograma

| Dia | Código | Texto |
|---|---|---|
| **Seg 15/09** | Jogo da Vida em C (double buffer, `pthread_barrier_t`, faixas de linhas). Valida a arquitetura antes do SLEUTH. | §3.0 |
| **Ter 16/09** | Escrever `globals.h` e `stubs.c`. Matrizes globais. `main.c` rodando N anos. Versão **serial** compilando e rodando em 20×20. | — |
| **Qua 17/09** | Harness de teste (semente fixa, dump, hash). `testgen.c` para dados artificiais. Baseline serial cronometrado. | §3.1, com a tabela de divergências |
| **Qui 18/09** | Substituir o RNG global pelo RNG ancorado à célula. Validar que a saída serial não mudou de caráter. Paralelizar `spr_phase4`. | — |
| **Sex 19/09** | Paralelizar `spr_phase1n3` e `spr_phase5`. Implementar as variantes A, B e C. | §3.2 |
| **Sáb 20 – Dom 21/09** | (sem código) | §2.1, §2.2, §2.3 — texto puro, bibliografia já levantada |
| **Seg 22/09** | Testes de corretude e determinismo: 1 vs 2 vs 4 threads. Caçar divergências. | — |
| **Ter 23/09** | Testes de desempenho: grades grandes × {1,2,4,8} threads × 3 variantes. Tabelas de speedup, eficiência, Amdahl. | §4 |
| **Qua 24/09** | *Colchão para atraso acumulado.* | §1, §1.1, §5 |
| **Qui 25/09** | — | Revisão, formatação ABNT, entrega ao orientador |

A quarta 24 é folga deliberada. O dia com maior risco de estouro é a terça 16: escrever os stubs do `globals.h` costuma revelar macros faltantes só no momento da compilação.

---

## 11. Riscos e planos de contingência

| Risco | Probabilidade | Mitigação |
|---|---|---|
| `globals.h` tem mais macros ocultas do que o previsto | Alta | Compilar cedo e iterar; a terça 16 é inteira para isso |
| Determinismo 1 vs N threads não fecha | Média | Cair para a alternativa de 4.5 (RNG por thread, equivalência estatística) e declarar a limitação no texto |
| Sem speedup mensurável nas grades disponíveis | Média | Escalar a grade (ver 4.6); em último caso, reportar o resultado negativo com análise de Amdahl, o que ainda é um resultado válido |
| Falta de tempo para as três variantes | Média | Implementar só a variante A; B e C viram trabalho futuro |
| Texto do referencial atrasa | Baixa | A bibliografia já está levantada e comentada; o fim de semana 20–21 está reservado só para isso |

---

## 12. Questões em aberto

1. **O dia 25 é entrega ao orientador para revisão, ou prazo final de submissão?** Se for revisão, há folga para ser mais ambicioso no capítulo 4. Se for final, a escrita do referencial deve ser puxada para esta semana.
2. Confirmar com o orientador se os stubs das funções externas são aceitáveis, ou se ele prefere que se baixe o código-fonte completo do SLEUTH no Project Gigalopolis e se trabalhe sobre a árvore original.
3. Confirmar o formato esperado das matrizes de entrada artificiais (há algum gerador ou dado de teste que ele já use?).
4. Qual máquina será usada para as medições de desempenho (número de núcleos físicos, SMT ligado ou desligado)? Isso precisa constar no capítulo 4.
