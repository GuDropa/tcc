# S05 — paralelização das varreduras de `spr_spread`

**Estágio:** S05 · **Status:** pendente · **Dia no plano:** 18/09 (escrito em 19/09)
**Mestre:** `../SPEC.md` · **Cita:** V2, V3, V5, V8, V15
**Alimenta o texto:** §3.3 (particionamento e barreiras) e §4.1 (determinismo)

**O recorte deste estágio mudou em relação ao §T do mestre**, por medição e não
por gosto. O mestre dizia "paralelização de `spr_phase4`"; a sonda
`harness/probe-s05.sh` mostrou que a fase 4 é **51,7 %** do relógio em dados
reais, e que há outras três varreduras `O(total_pixels)` ao lado dela somando
outros 37 %. Recortar o estágio na fase 4 sozinha poria um teto de Amdahl de
**2,07×** sobre o trabalho inteiro. Ver §D1 e §B19 do mestre.

---

## §G objetivo do estágio

Fazer as **quatro varreduras `O(total_pixels)`** de `spr_spread` rodarem em N
threads, particionadas por faixas de linhas e coordenadas por barreiras,
**sem que a saída mude um byte** em relação à execução com 1 thread (V3).

Este é o estágio em que o trabalho deixa de ser preparação e passa a ser
paralelismo. O S04 removeu o que impedia as threads de serem determinísticas;
aqui elas aparecem.

## §S escopo

**Entra:** o pool de threads e as barreiras; o particionamento por faixas de
linhas; a paralelização de quatro varreduras — zerar `delta`, a fase 4, os dois
filtros e o merge; os acumuladores por thread com redução (V5); a **variante A**
(mutex global) para a região crítica de `delta`, sem a qual o estágio não
entrega resultado correto; `--threads N` deixando de ser ignorado.

**Não entra:** as variantes **B** (sharding de locks) e **C** (CAS atômico), que
são do S06 — aqui entra só a que basta para estar certo. Não entram as fases
1n3 e 5, que são sequenciais por construção (§D7). Não entra a montagem da
lista de crescimento da fase 5, que é `O(N)` e seria a candidata seguinte, mas
cuja ordem alimenta `growth_index` e exigiria concatenação determinística —
risco que não cabe neste estágio (§D7).

**Não muda nenhuma regra do modelo.** Se o crescimento mudar de comportamento,
é bug deste estágio: com o gerador ancorado, o número de threads não é uma
entrada do modelo.

---

## §D entradas conhecidas

Levantamento de 19/09, medido no código real antes de escrever este spec (§R1
do mestre). A sonda está versionada em `harness/probe-s05.sh` e é reexecutável.

### D1 — onde está o tempo, e por que o recorte do mestre não servia

`demo200` 200×200, 20 anos, cenário de referência — **dados reais, 14,2 % de
área urbana**:

| trecho | ms | fração | `O(N)`? |
|---|---:|---:|:---:|
| zerar `delta` | 0,32 | 5,6 % | sim |
| fase 1n3 | 0,04 | 0,7 % | não |
| **fase 4** | **2,96** | **51,7 %** | sim |
| fase 5 | 0,59 | 10,2 % | parte |
| filtros (2× `util_condition_gif`) | 0,87 | 15,2 % | sim |
| merge | 0,95 | 16,6 % | sim |
| **total** | **5,73** | | |

**As quatro varreduras `O(N)` somam 89,1 %.** O teto de Amdahl deste estágio é
portanto ~9,2×; o do recorte antigo, só a fase 4, seria 2,07×. É a diferença
entre um capítulo de desempenho e uma nota de rodapé.

Em grades grandes a repartição se desloca mas a soma não: a 1000×1000, zerar
`delta` sobe para ~19 %, a fase 4 cai para ~17 %, os filtros vão a ~32 % e o
merge a ~21 % — e as quatro continuam valendo perto de 89 %.

**Cuidado ao ler as linhas artificiais.** A fase 4 parece barata nelas porque
o gerador artificial não produz cidade: a 1000×1000, mesmo com 150 anos, a
grade chega a 0,28 % urbana contra 14,2 % do `demo200`, e a fase 4
curto-circuita em célula não urbana. **A linha do `demo200` é a única com
fração urbana realista, e é ela que deve guiar o projeto.** A consequência
disso para o S08 está em §B19 do mestre.

### D2 — são quatro varreduras, não uma, e uma delas estava escondida

A fase 4 é a única que *parece* uma varredura, porque tem `for row / for col`
explícito. As outras três varrem igual:

- **zerar `delta`** — `util_init_grid`, um laço sobre `total_pixels`. Ficou
  de fora da primeira versão da própria sonda, o que subestimava o trabalho
  paralelizável do ano. É o lembrete de que "inicialização" também é `O(N)`.
- **os filtros** — dois `util_condition_gif` sobre `total_pixels`.
- **o merge** — o laço final de `spr_spread`.

E há uma quarta escondida **dentro da fase 5**: a montagem da lista de
crescimento é `for (iii = 0; iii < total_pixels; iii++)`. É por isso que a
fase 5 escala com o tamanho da grade apesar de fazer um número *fixo* de
sondagens aleatórias. Não entra neste estágio; ver §D7.

### D3 — o halo de escrita da fase 4 é de exatamente uma linha

`walkabout_row[8] = {-1,-1,-1, 0, 0, 1, 1, 1}`. Uma célula de origem `(row,
col)` só pode escrever em `delta` nas linhas `row-1`, `row` e `row+1`.

Consequência para o particionamento: uma thread que possua as linhas `[a, b]`
escreve no intervalo `[a-1, b+1]`. **Conflito entre threads só pode ocorrer nas
duas linhas de fronteira de cada faixa.** Numa grade de 200 linhas com 4
threads, são ~50 linhas por faixa e 2 delas contestáveis — 4 %. Numa de 2000
linhas, 0,4 %.

Isso é a medida do quanto a região crítica vai doer, e é o argumento que o S06
precisa para comparar as variantes: o mutex global serializa 100 % das
urbanizações para proteger 4 % delas.

A leitura de `z` por `util_count_neighbors` também cruza a fronteira, e é
**segura sem sincronização** porque `z` é imutável durante as fases (V2, §B2 do
mestre). É esse achado que torna o particionamento por faixas viável.

### D4 — o que cada varredura lê e escreve

| varredura | lê | escreve | acumula | precisa de |
|---|---|---|---|---|
| zerar `delta` | — | `delta` | — | nada; disjunto por faixa |
| fase 4 | `z`, `slp`, `excld` | `delta` | `og`, `stats_*` | **exclusão** (§D6) e redução |
| filtros | `delta`, `excld` | `delta` | — | nada; **estritamente por pixel** |
| merge | `z`, `delta`, `slp` | `z` | `num_growth_pix`, `average_slope` | **redução** (§D5) |

Os filtros são o caso mais simples do trabalho inteiro: `util_condition_gif`
decide cada pixel olhando só para aquele pixel. Não há vizinhança, não há
acumulador, não há ordem. Servem de caso-base didático no texto — o
particionamento perfeito, contra o qual os outros três se comparam.

### D5 — a redução de `average_slope` ameaça V3, e a saída é somar inteiro

O merge faz `(*average_slope) += (float) slp[i]`. Somar em ponto flutuante
**não é associativo**: repartir a soma em 4 parciais e depois somá-las dá um
resultado diferente de repartir em 2. Uma redução ingênua produziria, portanto,
um `average_slope` que **muda com o número de threads** — e V3 exige igualdade
byte a byte para todo N.

Seria uma quebra de V3 legítima e chata: nada errado com o paralelismo, só
aritmética de ponto flutuante.

**A saída é que os valores somados são inteiros.** `slp` é `GRID_P`, ou seja
`long *`. Acumular em `long` e converter uma única vez no fim é **exato,
independente de ordem e independente de N** — e de quebra mais preciso que o
original.

**Isso é seguro para V7 por causa de §B16 do mestre:** o `average_slope` que
`spr_spread` devolve é saída morta no SLEUTH original — nenhum `stats_Set*` o
consome, e a coluna `slope` do `avg.log` vem de `stats_circle`, que o protótipo
replica em `urban_mean_slope()`, fora de `spr_spread` e fora deste estágio. O
`compare_v7.sh` compara `decl_z`, não `cresc_decl`. Podemos tornar a soma exata
sem tocar no oráculo.

`num_growth_pix`, `og` e os contadores `stats_*` são inteiros e já são exatos
em qualquer ordem.

### D6 — a região crítica de `delta` precisa ser resolvida **já neste estágio**

O mestre coloca as variantes A/B/C no S06. Mas o S05 não pode entregar um
programa com corrida de dados: seria comportamento indefinido, não "uma versão
mais lenta". Então **o S05 entrega a variante A (mutex global)**, que é a que
basta para estar certo, e o S06 acrescenta B e C para comparar. V6 continua
valendo — as três são selecionáveis em tempo de execução pela mesma compilação;
só a data de chegada de cada uma muda.

**A exclusão não é só para evitar UB — é o que faz V3 valer.** O argumento:
duas origens que disputem a mesma célula-alvo produzem, *com* exclusão,
exatamente uma urbanização, e `delta` recebe `PHASE4G` seja qual for a
vencedora, porque dentro da fase 4 todas as escritas carregam a mesma etiqueta.
`og` é incrementado uma vez nas duas ordens. O resultado é idêntico
independentemente de quem chegou primeiro. **Sem** exclusão, as duas leem 0,
as duas escrevem e as duas incrementam `og` — e aí o número de threads vira
entrada do modelo.

Os sorteios de declividade e exclusão não entram nessa conta: estão ancorados à
**origem** (S04), então cada origem recebe o seu valor independentemente da
ordem.

Medida da frequência: 60 colisões em 20 anos de `demo200`, contra 1831
urbanizações — 3,2 %. É pequeno, e é exatamente por isso que o S06 tem o que
comparar.

### D7 — o que sobra serial, e por que

Depois deste estágio o trecho sequencial é a fase 1n3 (0,7 %) e a fase 5
(10,2 % no `demo200`, ~14 % em grade grande).

As duas são sequenciais **por construção**, não por preguiça: fazem um número
fixo de sondagens com dependência entre iterações (a fase 5 caminha pela
estrada, e cada passo depende do anterior). Paralelizá-las seria paralelizar
1 % dos sorteios ao custo de destruir a legibilidade — e §D1 do S04 já mostrou
que não vale.

**A exceção é a montagem da lista de crescimento da fase 5**, que é `O(N)` e
responde pela maior parte do custo dessa fase em grades grandes. Ela é
paralelizável — faixas com listas parciais concatenadas na ordem do índice da
faixa —, mas a ordem da lista alimenta `growth_index`, então errar a
concatenação muda o modelo em silêncio. **Fica declarada como limite deste
estágio e candidata natural ao S06**, e a decisão é registrada aqui para que
não vire esquecimento.

---

## §T tarefas

| id | st | tarefa | cita |
|----|----|--------|------|
| T1 | x | `par.c`/`par.h`: pool de N threads criado em `spr_spread`, faixas de linhas por particionamento estático, `pthread_barrier_t` entre as etapas. Uma thread por faixa, criadas e unidas uma vez por ano (§C: "disparadas a partir de `spr_spread`"). *(N−1 threads; a que chama é o worker 0 — §B1)* | V8 |
| T2 | x | `--threads N` deixa de ser ignorado e passa a valer; o número aparece no cabeçalho da saída, como `--rng` faz desde o S04. | V12 |
| T3 | x | Paralelizar as duas varreduras sem acumulador nem exclusão — zerar `delta` e os dois filtros. São o caso-base: disjuntas por faixa, sem nada compartilhado. **Faça primeiro**: valida o pool e as barreiras contra o caso mais simples. | V2, V8 |
| T4 | x | Merge paralelo com redução. `average_slope` passa a acumular em `long` e a converter uma vez no fim (§D5); `num_growth_pix` vira acumulador por thread somado na barreira. | V3, V5 |
| T5 | x | Contadores por thread para `og` e os cinco `stats_Increment*`, com redução na barreira. Nenhum incremento desprotegido sobrevive. *(as cinco `stats_Increment*` foram removidas, não sincronizadas)* | V5 |
| T6 | x | Fase 4 paralela com a **variante A**: mutex global em torno do read-modify-write de `spr_urbanize`. **Maior risco do estágio** — é a única etapa com escrita contestada (§D3, §D6). *(resolveu também a ponta solta do `--variant`: o padrão passou a ser `A`)* | V3, V6 |
| T7 | x | `run_checks.sh` ganha VS19–VS23. `--threads` entra na matriz de verificação. | V3 |
| T8 | x | Medir com 1, 2, 4 e 8 threads no `demo200` e registrar. **Não é a medição de desempenho do trabalho** — essa é o S08, com grades grandes e o `testgen` de §B19. Aqui o número serve para saber se o estágio funcionou, não para o texto. *(§B6, §B7, §B8)* | V11, V15 |

**Estágio concluído em 20/09.** As oito tarefas fecharam na ordem prevista e a
suíte roda VS1–VS23, com VS12 e VS13 ainda adiadas para o S08. O T3 antes do T6
cumpriu o que prometia: as varreduras sem nada compartilhado passaram de
primeira, então quando os contadores divergiram no T6 a causa já estava
cercada na região crítica e não na infraestrutura.

Ordem: T1 → T2 → T3 → T4 → T5 → T6 → T7 → T8.

O T3 antes do T6 é deliberado, pelo mesmo motivo que o S04 pôs T5 antes de T3:
as varreduras sem nada compartilhado exercitam o pool, as faixas e as barreiras
**sem** a região crítica no caminho. Se V3 quebrar no T3, o problema é a
infraestrutura de threads; se quebrar só no T6, é a exclusão. Separar os dois
custa uma tarefa e economiza uma depuração.

## §V verificação do estágio

VS1–VS18 continuam valendo, e **VS17 passa a ser a ferramenta de diagnóstico
principal**: se algo divergir aqui, rode-o antes de culpar a concorrência. Ele
responde "a ordem importa?" sem nenhuma thread envolvida.

- **VS19** — **V3:** `--threads 1`, `2`, `4` e `8` produzem dumps de `z`
  **idênticos byte a byte**, para a mesma semente, em `--rng anchored`, em 20
  anos de `demo200`. É a invariante central do trabalho. O hash por ano
  (`--hash`) localiza o primeiro ano divergente sem despejar a grade.
- **VS20** — as sete estatísticas também são idênticas entre contagens de
  threads, não só o `z` final. Inclui `og`, `num_growth_pix` e `average_slope`
  — este último é o que a redução ingênua quebraria (§D5).

  **Corrigida durante a execução, por §B5.** A redação original mandava
  verificar também que os cinco contadores de tentativa de urbanização fossem
  idênticos, e eles **não são** — nem entre contagens de threads, nem entre
  execuções com a mesma contagem. O que VS20 verifica agora:

  | quantidade | exigência |
  |---|---|
  | a tabela anual inteira (as sete estatísticas) | idêntica byte a byte |
  | `sucesso`, `já era urbana (z)` | idênticos |
  | `já crescida (delta)` + `declividade` + `área excluída` | **soma** idêntica |

  Não é afrouxamento: é a invariante certa. Cobrar identidade dos cinco seria
  cobrar do programa uma propriedade que a exclusão mútua não dá — e faria a
  suíte reprovar código correto, que é a armadilha 4 do HANDOFF. O porquê está
  em §B5.
- **VS21** — `--threads 1` reproduz **bit a bit** a saída serial de antes do
  S05, em ambos os modos de RNG. É o VS18 deste estágio: separa "mudou porque
  paralelizei" de "mudou porque errei ao reorganizar o código".

  **A rede já está colhida**, com a árvore ainda serial, antes de qualquer
  thread:

  | referência | modo |
  |---|---|
  | `tests/golden/z-pre-S05-anchored.txt` | `--rng anchored` |
  | `tests/golden/z-pre-S04.txt` | `--rng legacy` — conferido idêntico ao dump legacy de hoje, então não foi duplicado |

  `run-pre-S05-anchored.txt` guarda a saída com `--hash`, que localiza o
  primeiro ano divergente sem despejar a grade. A lição do S04 foi que colher
  a rede *antes* é o que permite atribuir a causa depois; aqui ela foi colhida
  antes mesmo de o estágio começar.
- **VS22** — V7 continua valendo: `--threads 1 --rng legacy` reproduz o SLEUTH
  original. A suíte já roda isso em VS11; aqui só se garante que o `--threads`
  explícito não o altera.
- **VS23** — **ThreadSanitizer limpo** em `--threads 4`, 5 anos, 20×20 e
  `demo200`. É o instrumento que pega a corrida que V3 pode não pegar: uma
  corrida benigna na etiqueta de `delta` produz saída idêntica e continua sendo
  comportamento indefinido. `make tsan`, ao lado de `make asan`.

**VS23 merece nota.** V3 e TSan medem coisas diferentes e as duas são
necessárias: V3 diz "o resultado é o mesmo", TSan diz "o programa é bem
definido". §D6 mostra um caso em que o resultado seria o mesmo e o programa
estaria errado — duas threads escrevendo `PHASE4G` na mesma célula. Passar em
V3 e reprovar em TSan é exatamente o cenário a temer, porque é o que sobrevive
a uma bateria de testes e falha num compilador diferente.

---

## §B achados do estágio

Execução de 20/09. As oito tarefas fecharam e VS1–VS23 passam. Os achados
abaixo estão em ordem de descoberta; **B5, B7 e B8 são os que mudam o que o
texto vai dizer**, e B5 sobe para o mestre porque refina V3.

### B1 — o pool é de N−1 threads, e nasce a cada ano

O §T1 diz "uma thread por faixa, criadas e unidas uma vez por ano". São N
faixas, mas se criam **N−1 threads**: a thread que chamou `spr_spread` é o
worker 0. Desvio pequeno e deliberado, e com uma consequência que vale mais
que a economia de uma thread — **com `--threads 1` não se cria thread nenhuma,
não se toca em barreira nenhuma e não se trava nada**. O caminho de uma thread
é o serial de antes do estágio mais a conta da faixa, e é por isso que VS21
pode exigir igualdade *bit a bit* contra a saída pré-S05 em vez de
"equivalência".

Criar e unir **por ano** é o que o §T1 manda, e foi o que se fez. B6 mede o
que isso custa.

### B2 — `pthread_barrier_t` não existe sob `-std=c11` estrito

O `Makefile` compila com `-std=c11`, o que define `__STRICT_ANSI__` e faz a
glibc esconder tudo o que não é C puro. `pthread_barrier_t` é POSIX 2001, não
C11. O erro é `unknown type name 'pthread_barrier_t'`, que **soa como pthread
faltando e não é** — o `-pthread` estava lá desde o S02.

O conserto é `#define _POSIX_C_SOURCE 200809L` no topo do `par.c`, antes de
qualquer include. Ficou no arquivo e não no `CFLAGS` para valer só onde é
necessário, e para que quem abrir o `par.c` veja o motivo junto com o sintoma.

### B3 — `GRID_P a = x, b = y` declara `b` como `long`

`GRID_P` é `#define GRID_P PIXEL *`, macro e não `typedef`. Numa declaração
múltipla o `*` gruda só no primeiro nome, então
`GRID_P z = g_z, delta = g_delta;` declara `delta` como `long`. Custou três
erros de compilação numa linha que parecia óbvia.

Uma declaração por linha no código novo. O código herdado já fazia assim, o
que em retrospecto não era só estilo.

### B4 — há uma quinta varredura `O(N)`, e §D1 não a media

`*pop = util_count_pixels (total_pixels, z, GE, PHASE0G)`, no fim de
`spr_spread`, é um laço sobre `total_pixels` como qualquer outro. A marcação
antiga da sonda parava no merge, então **essa varredura ficava fora do total**
— o denominador de §D1 era menor do que o ano de verdade.

A correção não muda a decisão do estágio (as quatro varreduras nomeadas
continuam sendo as certas), mas muda os percentuais de §D1: medida agora, a
`pop` vale **6,1 %** do ano no `demo200` serial (0,48 de 7,85 ms) e **10,0 %** a
2000×2000 (16,21 de 162,41 ms).

É o mesmo erro que §D2 registra sobre o "zerar delta", cometido de novo pela
mesma razão — a sonda mede o que alguém lembrou de marcar. Fica fora do escopo
deste estágio de propósito (§S nomeia quatro varreduras), e é candidata natural
ao S06 junto com a montagem da lista de crescimento de §D7.

### B5 — a exclusão mútua torna o MODELO independente da ordem, não a INSTRUMENTAÇÃO

**É o achado central do estágio, e o único que refina uma invariante.**

§D6 argumenta que a variante A basta para V3: duas origens que disputem o mesmo
alvo produzem exatamente uma urbanização, `delta` recebe `PHASE4G` seja qual
for a vencedora, `og` é incrementado uma vez. **Tudo isso se confirmou** — o
`z` final é idêntico byte a byte para 1, 2, 4, 8 e 16 threads, e a tabela anual
inteira (`sng sdc og rt cresc pop cresc_decl decl_z`) também.

O que §D6 não previu são os **cinco contadores de tentativa de urbanização**.
Medidos com `harness/probe-contadores.sh`, `demo200`, 20 anos, 6 repetições por
configuração:

| contador | 1 thread | 2 | 4 | 8 | 16 |
|---|---:|---:|---:|---:|---:|
| sucesso | 1816 | 1816 | 1816 | 1816 | 1816 |
| já era urbana (z) | 4868 | 4868 | 4868 | 4868 | 4868 |
| já crescida (delta) | 58 | 59 | 60 | 59–60 | 59–61 |
| declividade | 597 | 596 | 595 | 595–596 | 594–596 |
| área excluída | 111 | 111 | 111 | 111 | 111 |
| **soma das três últimas** | **766** | **766** | **766** | **766** | **766** |

**O mecanismo.** A cascata de `spr_urbanize` testa `delta == 0` primeiro e a
declividade depois. Quem perde a corrida por um alvo é contado em `delta` se
chegou **depois** do vencedor, e em `declividade`/`excluída` se chegou **antes**
e reprovou por conta própria. Com o mutex, quem chega primeiro é o escalonador
que decide — então a repartição varia **inclusive entre execuções com o mesmo
N** (veja as faixas de 8 e 16 threads acima: é corrida, não função de N).

**O que se conserva, e por quê.** `sucesso` é o número de células urbanizadas,
que é determinístico. `z_failure` depende só de `z`, imutável durante as fases
(V2). E a soma `delta + declividade + excluída` é o número total de tentativas
que passaram no teste de `z` e não urbanizaram — invariante porque o conjunto
de tentativas é determinístico; só o **rótulo** de cada reprova é que depende
da ordem.

**Por que não dá para consertar neste estágio.** Reproduzir a repartição serial
exigiria saber, para cada alvo disputado, qual contendor vem primeiro **na
ordem de varredura** — que é exatamente a ordem global que o particionamento
destrói. Daria: seria um algoritmo de duas passadas, ou um CAS de "menor índice
vence" com recontagem. É muito além da variante A e muda o modelo de execução;
não cabe aqui.

**Reordenar a cascata também não serve**, e vale registrar por que, porque é a
tentação óbvia: avaliar declividade e exclusão *antes* do teste de `delta`
tornaria os cinco contadores independentes de ordem, já que os dois testes
dependem só de dados imutáveis e da chave da origem. Mas mudaria a contagem no
caso **serial** — uma célula com `delta` já tomado passaria a ser contada em
declividade —, e isso quebra VS21 e V7. O oráculo manda.

**Consequência para V3** (sobe para o mestre, §R4): V3 continua valendo sobre a
**saída do modelo**, e VS20 passa a verificar o que de fato é invariante em vez
de cobrar uniformidade que o programa não tem. Cobrar "os cinco contadores
idênticos" seria a armadilha 4 do HANDOFF em forma nova — critério errado
disfarçado de bug.

**Para o texto (§3.3 e §4.1).** É um parágrafo melhor do que qualquer um que o
plano previa: a exclusão mútua garante que o **resultado** não dependa da
ordem, e não torna a **medição do processo** independente da ordem. Quem
instrumenta uma região crítica está medindo a corrida, e a instrumentação é
parte da corrida.

### B6 — o pool por ano custa mais que o ano inteiro, em grade pequena

Medido com `harness/probe-s05-threads.sh` (mediana de 5, `-DPROBE_PHASE_TIMING`,
`clock_gettime(CLOCK_MONOTONIC)`, I/O fora da região medida conforme V11).
`demo200`, 20 anos, em ms:

| threads | pool | zera | 1n3 | fase 4 | fase 5 | filtros | merge | pop | total |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 0,01 | 0,46 | 0,02 | 3,39 | 0,89 | 1,58 | 0,94 | 0,48 | **7,85** |
| 2 | 2,11 | 2,13 | 0,54 | 3,99 | 1,59 | 1,82 | 1,42 | 0,53 | 14,25 |
| 4 | 5,46 | 2,70 | 1,90 | 5,75 | 2,66 | 2,81 | 1,75 | 0,73 | 22,08 |
| 8 | 9,94 | 3,92 | 3,27 | 4,59 | 4,57 | 4,32 | 2,56 | 0,70 | 33,59 |

Com 8 threads o `pthread_create` sozinho (9,94 ms) custa **mais que o ano
serial inteiro** (7,85 ms), e as quatro varreduras que o estágio paralelizou
ficaram 2,4× mais lentas em vez de mais rápidas. São 20 anos × 7 criações.

Isso **não contradiz** §B6 nem V15 do mestre, que já diziam que grade pequena
não mostra speedup — mas a magnitude é nova, e a causa é específica: não é
contenção do mutex, é o custo de montar o pool a cada ano somado ao das seis
barreiras por ano. A 200×200, cada varredura custa décimos de milissegundo por
ano, que é a mesma ordem de grandeza de uma barreira com 8 participantes.

**Dívida registrada para o S06:** um pool **persistente** — threads criadas uma
vez na inicialização e dirigidas por barreiras entre os anos — elimina a coluna
`pool` inteira. O §T1 pediu criação por ano e foi o que se entregou; a medição
agora diz que essa escolha tem preço, e qual.

### B7 — o teto das quatro varreduras é a BANDA DE MEMÓRIA, não Amdahl

Mesma sonda, grades artificiais grandes, 5 anos, ms, coluna "4 varred." = zerar
delta + fase 4 + filtros + merge:

| grade | 1 thread | 2 | 4 | 8 | ganho a 8 |
|---|---:|---:|---:|---:|---:|
| 1000×1000 | 29,87 | 22,53 | 17,08 | 14,27 | **2,09×** |
| 2000×2000 | 122,54 | 78,43 | 64,52 | 54,62 | **2,24×** |

**O particionamento funciona** — é a resposta que a T8 pediu. Mas o ganho
satura em ~2,2× com 8 threads numa máquina de 12 núcleos, muito longe do teto
de Amdahl de 9,2× que §D1 calculou.

> ⚠ **Correção de 20/09 (sessão 11), registrada sem reescrever o texto acima
> — §V12.** São **6 núcleos físicos**, não 12: os 12 são lógicos, com SMT 2×
> (§B30 do mestre). A 8 threads a medida já excede os físicos.
>
> **A conclusão deste §B sobrevive inteira**, e vale dizer por quê: mesmo contra
> 6 núcleos, 2,24× é 37 % de eficiência — continua muito abaixo de qualquer teto
> plausível. E a evidência que de fato sustenta "é banda de memória" não é a
> comparação com a contagem de núcleos: é a **abertura por varredura logo
> abaixo**, em que o ganho cresce com o trabalho por byte (2,06× na escrita pura
> contra 3,08× na fase 4). Essa tabela não depende de quantos núcleos a máquina
> tem.
>
> O que **não** se deve mais repetir é a moldura "numa máquina de 12 núcleos",
> porque ela infla a distância até o teto. Ver §D6 do spec do S08.

A causa aparece ao abrir por varredura, a 2000×2000, de 1 para 8 threads:

| varredura | 1 thread | 8 threads | ganho | trabalho por byte |
|---|---:|---:|---:|---|
| zerar delta | 29,29 | 14,22 | 2,06× | uma escrita |
| merge | 24,11 | 11,86 | 2,03× | uma leitura, uma escrita condicional |
| filtros | 48,01 | 18,56 | 2,59× | duas passadas, comparação |
| fase 4 | 21,80 | 7,07 | **3,08×** | vizinhança, sorteio |

**Quanto mais trabalho por byte lido, melhor escala.** "Zerar delta" é um laço
de escrita puro e escala pior de todas; a fase 4, a mais cara por pixel, escala
melhor. É a assinatura clássica de saturação de banda de memória, não de
contenção nem de desbalanceamento.

**Consequência para §D1 e para o texto (§4.2).** O cálculo de Amdahl de §D1
está aritmeticamente certo e é uma previsão ruim, porque supõe que as quatro
varreduras paralelizam perfeitamente. Elas não paralelizam: três das quatro são
*memory-bound*. O teto real deste estágio é dado pela banda de memória da
máquina, não pela fração serial do programa. **Isso reescreve o argumento de
§4.2** — o limite do trabalho não é "quanto sobrou de serial", é "quanto a
máquina consegue ler por segundo".

Vale medir a banda da máquina no S08 para publicar a conta fechada.

### B8 — paralelizar as varreduras deixa as fases SERIAIS mais lentas

Ainda a 2000×2000, de 1 para 8 threads, nas etapas que **não** foram
paralelizadas:

| etapa serial | 1 thread | 8 threads | variação |
|---|---:|---:|---:|
| fase 5 | 23,52 | 34,22 | **+45 %** |
| pop | 16,21 | 23,25 | **+43 %** |

O `pop` é o controle limpo do achado: ele é medido em `spr_spread` **depois**
de `par_run` retornar, fora de qualquer barreira, então o número não pode estar
contaminado por espera. É o mesmo laço serial, sobre os mesmos dados, 43 % mais
lento só porque oito threads acabaram de percorrer a grade.

A explicação provável é localidade: depois da região paralela as linhas de
cache da grade estão espalhadas pelos caches privados de 8 núcleos, e a thread
que roda a etapa serial tem de puxá-las de volta. **Não foi confirmada** — não
se mediu contador de cache —, e por isso fica registrada como observação e não
como mecanismo.

Importa porque é um custo que nenhuma conta de Amdahl prevê: a fração serial
não só deixa de acelerar, como **piora** quando o resto acelera. Se o efeito se
confirmar no S08, é argumento forte para o candidato de §D7 (paralelizar a
montagem da lista de crescimento da fase 5) — não pelo ganho da própria etapa,
mas para não pagar a volta.

### B9 — §B20 do mestre vale para a fase 4, e não para as outras três

§B20 diz que o gerador artificial não produz cidade (0,28 % urbana a
1000×1000 contra 14,2 % do `demo200`) e conclui que medir perfil em grade
artificial grande mede uma grade *vazia*.

**Verdadeiro para a fase 4**, que curto-circuita em célula não urbana. **Falso
para as outras três varreduras:** zerar `delta`, os dois filtros e o merge
percorrem `total_pixels` e fazem o mesmo trabalho por pixel havendo cidade ou
não. A fração urbana não entra na conta delas.

Isso abriu o único caminho que este estágio tinha para ver as varreduras
trabalhando em escala sem esperar o `testgen` do S08, e é o que sustenta B7 e
B8. A coluna `fase 4` das tabelas de grade artificial continua sendo para
ignorar — está marcada como tal na saída da sonda.

Não substitui a dívida de §B20: o S08 continua precisando de fração urbana
realista em grade grande para afirmar speedup do **trabalho**. Reduz o
tamanho da dívida, não a quita.

### B10 — a corrida de dados não se manifestou, e o TSan a pegou

`--variant none` com 8 threads no `demo200`, 20 anos: **6 de 6 execuções
produziram o `z` correto**, byte a byte igual ao serial, e os cinco contadores
dentro da mesma faixa da variante A. Sem exclusão nenhuma no
read-modify-write.

O ThreadSanitizer acusa na primeira execução, e nomeia a linha:

```
WARNING: ThreadSanitizer: data race
  Read of size 8  ... spr_urbanize  spread.c:795   <- delta[OFFSET] == 0
  Previous write  ... spr_urbanize  spread.c:812   <- delta[OFFSET] = pixel_value
```

É exatamente o cenário que o §V deste spec dizia ser o de temer, agora medido
em vez de suposto: **passar em V3 e reprovar no TSan**. Um projeto que só
tivesse V3 como critério teria declarado a variante `none` correta e embarcado
comportamento indefinido.

Por isso VS23 tem duas metades, como VS17: `A` tem de sair limpa e `none` tem
de **acusar**. Uma verificação que passasse nos dois lados não estaria medindo
nada.

E é por isso que o padrão de `--variant` passou a ser `A` (T6): `none` continua
selecionável, explicitamente e com aviso alto, como modo de demonstração para
o §2.2 do texto — nunca por omissão.

---

## §N notas para o texto

**Para §3.3 (particionamento).** As quatro varreduras deste estágio formam uma
escala de dificuldade que dá a espinha da seção, e cada degrau precisa de um
mecanismo a mais que o anterior:

| varredura | o que compartilha | mecanismo |
|---|---|---|
| filtros | nada — decide cada pixel olhando só para ele | faixa e pronto |
| zerar `delta` | nada — escrita disjunta por faixa | faixa e pronto |
| merge | acumuladores | **redução** por thread na barreira |
| fase 4 | escrita contestada na fronteira | **exclusão mútua** |

É uma tabela melhor do que a lista de "problemas de concorrência" que o plano
original previa, porque mostra que os mecanismos **não são intercambiáveis**:
usar mutex onde bastava faixa é desperdício, e usar faixa onde era preciso
redução é bug.

**Para §4.1 (determinismo).** O ponto não óbvio é §D5: o primeiro risco a V3
neste estágio **não é corrida de dados, é ponto flutuante**. Somar os mesmos
números em ordem diferente dá resultado diferente, e repartir um laço entre N
threads muda a ordem da soma por definição. Um leitor que associe "determinismo"
a "exclusão mútua" não vê isso chegando — e o conserto não é uma trava, é
perceber que os valores somados eram inteiros o tempo todo.

Junto com §3.2 do S04 (onde a solução também não era sincronizar, era eliminar
o estado), fecha o argumento do trabalho: **das ameaças ao determinismo, a
minoria se resolve com exclusão mútua.** As outras se resolvem com projeto.

**Para §4.2 (desempenho).** §D1 é o parágrafo honesto: paralelizar "a fase 4",
que era o plano, teria teto de 2,07×; foi a medição que revelou os outros 37 %.
Vale publicar a tabela inteira, inclusive a linha "zerar `delta`", que ficou de
fora da primeira versão da própria sonda — é um exemplo pequeno e verdadeiro de
que o perfil se mede, não se adivinha.

E vale declarar o limite: sobra ~11 % serial, quase tudo na fase 5, cuja maior
parte é uma varredura `O(N)` que **daria** para paralelizar e que foi deixada
de fora por risco, não por impossibilidade (§D7).
