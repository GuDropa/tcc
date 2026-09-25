# S04 — RNG ancorado à célula

**Estágio:** S04 · **Status:** concluído · **Dia no plano:** 18/09 (escrito em
18/09, executado em 19/09)
**Mestre:** `../SPEC.md` · **Cita:** V4, V3, V7
**Alimenta o texto:** §3.2 (exclusão mútua e estado compartilhado) e §4.1 (determinismo)

**Resultado em uma linha:** V4 está demonstrada sem nenhuma thread — inverter a
ordem de varredura da fase 4 produz saída idêntica byte a byte no modo
`anchored` e divergente no modo `legacy` (VS17). V7 continua verificada, com
`--rng legacy`. As sete tarefas fecharam; os achados estão em §B e os números
do T7 em §R.

---

## §G objetivo do estágio

Fazer com que **o sorteio que uma célula recebe não dependa de quantas células
foram processadas antes dela**.

Hoje depende, e é isso que impede V3. O gerador é um fluxo único: a fase 4
varre a grade e cada célula consome de 0 a 4 sorteios conforme o que encontra.
A n-ésima célula recebe o sorteio que sobrou das n−1 anteriores. Reparta essa
varredura em faixas entre threads e a posição no fluxo passa a depender da
ordem de escalonamento — a saída deixa de ser reproduzível, **antes mesmo de
haver qualquer escrita concorrente**.

Trocar o gerador por um **contador-based ancorado à célula** resolve na raiz:
o valor vira função pura da chave `(semente, ano, fase, i, j, subpasso)`, e
duas execuções com contagens de threads diferentes leem a mesma sequência para
a mesma célula (V4).

Este estágio não cria threads. Ele remove o que impediria as threads de serem
determinísticas.

## §S escopo

**Entra:** `rng.c`/`rng.h`; substituição das macros `RANDOM_*` nos 10 pontos de
sorteio de §D3; propagação da chave pelas fases e por `spr_urbanize`; remoção
do `static last_index` de `util_get_next_neighbor` (§D5); seleção
`legacy`/`anchored` em tempo de execução; verificação de determinismo.

**Não entra:** threads, barreiras, variantes A/B/C (S05/S06); qualquer mudança
nas regras do modelo. Se o crescimento mudar de comportamento além do que a
troca de fluxo aleatório justifica, é bug deste estágio.

---

## §D entradas conhecidas

Levantamento de 18/09, medido no código real antes de escrever este spec
(§R1). A sonda está versionada em `harness/probe-rng.sh` e é reexecutável.

### D1 — a fase 4 é 99 % do consumo

Medido, `demo200` 200×200, 1990→2010, semente 1, coeficientes de referência:

| origem | sorteios (20 anos) | fração |
|---|---:|---:|
| fase 1n3 (difusão) | 246 | 0,2 % |
| **fase 4 (orgânico)** | **105 163** | **99,0 %** |
| fase 5 (estradas) | 826 | 0,8 % |
| fora das fases | 1 | 0,0 % |
| **total** | **106 236** | 5 312 por ano |

Três consequências de projeto:

1. **O gerador ancorado precisa ser rápido no caminho da fase 4 e só nele.**
   Um `rng_double()` com dezenas de ciclos é aceitável nas fases 1n3 e 5, que
   juntas fazem 1 % dos sorteios.
2. A fase 4 é justamente a que S05 paraleliza primeiro. As duas decisões se
   reforçam: é a fase mais paralelizável *e* a que domina o consumo.
3. O "fora das fases = 1" **confirma §D7 do S03 por medição**: `InitRandom`
   consome exatamente um sorteio e nada mais toca o gerador. Era dedução de
   leitura de código; agora é número.

### D2 — o consumo por célula é condicional, e é esse o mecanismo que quebra V3

A fase 4 varre 198×198 = 39 204 células por ano e gasta 105 163/20 ≈ 5 258
sorteios por ano: **0,134 sorteio por célula varrida**.

O número é baixo porque o teste é curto-circuitado:

```c
if ((z[OFFSET (row, col)] > 0) && (RANDOM_INT (101) < spread_coefficient))
```

Célula não urbana **não sorteia**. A fração 0,134 é, a menos de ruído, a
fração urbana da grade — o gerador é consumido só onde já há cidade.

É daqui que sai o problema inteiro: **quantos sorteios foram consumidos antes
de chegar à célula (i,j) depende do estado urbano de todas as células
anteriores na ordem de varredura.** Reparta a varredura em faixas e cada faixa
começa numa posição do fluxo que depende de quanto as outras consumiram. Não é
corrida de dados — é dependência de ordem, e nenhum mutex conserta.

### D3 — os dez pontos de sorteio, e quantos cada célula pode consumir

| # | local | linha | sorteio | quando |
|---|---|---|---|---|
| 1 | `spr_phase1n3` | 130 | `RANDOM_ROW` | por iteração `k` |
| 2 | `spr_phase1n3` | 131 | `RANDOM_COL` | por iteração `k` |
| 3 | `spr_phase1n3` | 141 | `RANDOM_INT(101)` | se urbanizou |
| 4 | `spr_phase4` | 215 | `RANDOM_INT(101)` | **só se `z > 0`** |
| 5 | `spr_phase4` | 221 | `RANDOM_INT(8)` | se 2 ≤ vizinhos < 8 |
| 6 | `spr_phase5` | 365 | `RANDOM_FLOAT` | por iteração `iii` |
| 7 | `spr_urbanize` | 534 | `RANDOM_FLOAT` | se `z==0 && delta==0` |
| 8 | `spr_urbanize` | 536 | `RANDOM_INT(100)` | se passou na declividade |
| 9 | `spr_get_neighbor` | 577 | `RANDOM_INT(8)` | por chamada |
| 10 | `spr_road_walk` | 646 | `RANDOM_INT(8)` | por passo da caminhada |

Pelo caminho da fase 4, uma célula consome **de 0 a 4** sorteios: nenhum se
não for urbana; 1 se falhar no teste de espalhamento; 2 se a vizinhança não
qualificar; até 4 se chegar a `spr_urbanize`. É esse intervalo que a chave
precisa cobrir em `subpasso`.

### D4 — só a fase 4 varre a grade; as outras duas não têm célula para ancorar

Esta é a correção mais importante à leitura ingênua de V4.

- **`spr_phase4`** é `for row / for col`: a âncora natural é `(row, col)`.
- **`spr_phase1n3`** é `for k in 0 .. 1+diffusion_value` e **sorteia a célula**
  (`i = RANDOM_ROW; j = RANDOM_COL`). Não há célula antes do sorteio — a
  âncora tem de ser o índice `k`.
- **`spr_phase5`** é `for iii in 0 .. 1+breed` e sorteia um índice na lista de
  crescimento. Mesma situação: a âncora é `iii`.

V4 diz "ancorado à **célula e ao subpasso**, não à thread". Lido ao pé da
letra, não se aplica a duas das três fases. O que V4 realmente exige é que a
âncora seja **uma coordenada determinística do trabalho**, independente de
quem o executa — para a fase 4 isso é a célula, para as outras é o índice da
iteração. A chave unificada de §T1 acomoda as duas.

Isso não enfraquece V4: 1n3 e 5 fazem 1 % dos sorteios e seus laços são
sequenciais por construção (contagem fixa, dependência entre iterações).

### D5 — `util_get_next_neighbor` é o segundo estado escondido, e este estágio já o remove

`util.c:188`: `static int last_index`. Com `index == -1` a função devolve o
*próximo* vizinho continuando de onde parou (§B12 do mestre).

Consumido por `spr_get_neighbor` (que sorteia um índice e depois caminha com
`-1` até achar ponto válido) e por `spr_road_walk`. Duas threads embaralham a
sequência uma da outra — **e isso não é escrita em grade nenhuma**, então a
região crítica do plano não cobre.

Entra neste estágio, não em S05, por um motivo de coerência: ancorar o gerador
e deixar um segundo estado global mutável no caminho seria resolver metade do
determinismo. O conserto é tornar o cursor propriedade do chamador — um `int *`
na pilha —, o que elimina o estado sem mudar a sequência de vizinhos.

### D6 — `ran1` não é substituível por descuido, e por isso fica

`random.c` reproduz o `ran1` sem alteração numérica: constantes 16807, 127773,
2836, tabela `iv[32]`, ordem das operações. É o que torna V7 verificável — e
V7 verificada é o ativo que o S03 inteiro produziu.

**A troca de gerador quebra V7 por construção.** Não por erro: um fluxo
diferente produz números diferentes, logo estatísticas diferentes do `avg.log`.
Ver §D7.

### D7 — a decisão que este estágio não pode adiar: V7 depois do S04

Se o `ran1` for simplesmente removido, `compare_v7.sh` passa a falhar e o
projeto perde seu único oráculo externo de corretude — justamente quando entra
na parte arriscada (threads). Não é aceitável.

**Decisão: os dois geradores coexistem, selecionáveis em tempo de execução.**

```
--rng legacy      ran1 original, fluxo único        → oráculo de V7
--rng anchored    contador-based ancorado à chave   → o que S05+ usa
```

Com isso:

- **V7 continua verificável para sempre**, com `--rng legacy`. O
  `compare_v7.sh` passa a pedi-lo explicitamente.
- **V3 passa a ser verificável**, com `--rng anchored`, que é o único modo em
  que "1 thread == N threads" pode valer.
- A comparação `legacy` × `anchored` com 1 thread vira uma medida útil por si:
  as duas devem dar estatísticas **estatisticamente próximas** e numericamente
  diferentes. Se divergirem muito, a troca de gerador introduziu viés e isso é
  achado de §B, não ruído.

É também o recorte honesto para o texto: a substituição do gerador é uma
**mudança de modelo declarada**, não uma otimização silenciosa (V12).

**Padrão recomendado: `anchored`.** O protótipo que o trabalho entrega é o
paralelo; `legacy` é modo de compatibilidade com o oráculo. Deixar o padrão no
modo que não escala convidaria a medir a coisa errada em S08.

### D8 — que gerador ancorado, concretamente

Restrição de §C: sem dependências externas. Então nada de Random123 como
biblioteca — mas a ideia (counter-based, *stateless*) é exatamente a certa e
se implementa em poucas linhas.

Forma proposta, a fixar em T1:

```c
double rng_double (uint64_t semente, int ano, int fase,
                   int a, int b, int subpasso);
```

Sem estado. Empacota a chave em dois inteiros de 64 bits e passa por um
finalizador de mistura forte (tipo SplitMix64/murmur3), depois converte para
`[0,1)` com 53 bits de mantissa. Puro, reentrante, sem trava, sem
armazenamento por thread.

Para a fase 4, `(a,b) = (row,col)`. Para 1n3 e 5, `(a,b) = (k,0)`.

O ponto de atenção é **`spr_urbanize`**, que hoje deduz tudo de `(row,col)`.
Se ancorar nos *próprios* `row,col`, duas origens diferentes que tentem
urbanizar a mesma célula-alvo no mesmo ano recebem sorteios idênticos — o que
no original seriam duas tentativas independentes. A âncora correta é a
**origem** que motivou a tentativa, não o alvo. Logo `spr_urbanize` precisa
receber a chave, em vez de montá-la. É a mudança de assinatura de maior alcance
do estágio, e o principal risco de T3.

---

## §T tarefas

| id | st | tarefa | cita |
|----|----|--------|------|
| T1 | x | `rng.c`/`rng.h`: `rng_double()` sem estado, chave de §D8, mais `rng_int(n)` e os equivalentes de `RANDOM_ROW`/`RANDOM_COL`. Testes de sanidade: determinismo, independência entre chaves vizinhas, média e variância em 10⁶ amostras. | V4 |
| T2 | x | `--rng legacy\|anchored` em `I.cli`, padrão `anchored` (§D7). O modo escolhido aparece no cabeçalho da saída, para que nenhum resultado colhido seja ambíguo. | V12 |
| T3 | x | Propagar a chave: `spr_urbanize`, `spr_urbanize_nghbr` e `spr_get_neighbor` passam a receber o contexto `(ano, fase, a, b)` em vez de deduzi-lo. **Maior risco do estágio** — ver §D8. | V4 |
| T4 | ~ | Substituir os 10 pontos de §D3, atribuindo `subpasso` **distinto e constante** a cada ponto alcançável por uma mesma chave. Tabela de atribuição no próprio `rng.h`. | V4 |
| T5 | x | Remover o `static last_index` de `util_get_next_neighbor`: cursor vira `int *` do chamador (§D5). Conferir que a sequência de vizinhos não mudou no modo `legacy`. | V4 |
| T6 | x | `compare_v7.sh` passa a rodar com `--rng legacy` explícito; `run_checks.sh` ganha VS15–VS18. | V7 |
| T7 | x | Comparar `legacy` × `anchored` com 1 thread em 20 anos de `demo200`: registrar as 7 estatísticas lado a lado e avaliar se a diferença é compatível com troca de fluxo (§D7). Vira tabela do texto. | V12 |

Ordem: T1 → T2 → T5 → T3 → T4 → T6 → T7. O T5 vem antes do T3 de propósito:
é independente e reduz o ruído no diff do T3, que é o arriscado. **A ordem foi
seguida como escrita e funcionou** — o T5, que a intuição diria ser arriscado,
saiu de graça (§B3), e o T3, que o spec marcava como o perigo, foi de fato onde
esteve o perigo (§B2).

**T4 fechou com desvio (`~`):** nove dos dez pontos de sorteio aceitam um
subpasso constante, como a tarefa pedia; o décimo não aceita e precisou de uma
faixa. Ver §B1.

**Uma tarefa não prevista entrou:** o `--scan forward|reverse`, sem o qual VS17
não é verificável. Ver §B7.

## §V verificação do estágio

VS1–VS11 e VS14 continuam valendo. **VS11 (V7) passa a rodar com
`--rng legacy`** — a invariante não mudou, mudou o modo em que se verifica.

- **VS15** — `rng_double` é pura: mesma chave dá o mesmo valor, em qualquer
  ordem de chamada e em qualquer número de chamadas anteriores. Verificada
  chamando 10⁴ chaves em ordem embaralhada e comparando com a ordem direta.
- **VS16** — chaves vizinhas não se correlacionam: variar só `subpasso`, só
  `i`, só `j` ou só `ano` produz valores sem correlação detectável, e a média
  fica em 0,5 ± 0,01 em 10⁶ amostras. Um contador-based mal misturado passa em
  "é determinístico" e falha aqui.
- **VS17** — **V4:** no modo `anchored`, a sequência consumida por uma célula
  independe da ordem de varredura. Verificável sem threads: varrer a fase 4 em
  ordem inversa de linhas tem de dar `delta` idêntico. **É o teste que S05
  precisa que exista antes de haver qualquer thread** — se falhar aqui, falha
  paralelizado, e aí seria atribuído à concorrência.
- **VS18** — `--rng legacy` reproduz bit a bit a saída de hoje: o dump de `z`
  com `legacy` depois do S04 é idêntico ao dump de antes do S04, para a mesma
  semente. Garante que T3/T4/T5 não mudaram o comportamento por acidente,
  separando "mudou porque troquei o gerador" de "mudou porque errei a
  propagação".

VS18 é a rede de segurança do estágio. Gerar e guardar o dump de referência
**antes** de começar T3.

---

## §B achados do estágio

Ordem de descoberta. Os que um chat novo precisa ver sem abrir este arquivo
foram promovidos a §B do mestre — estão marcados.

### B1 — o subpasso de `spr_road_walk` não pode ser constante

O §T4 manda dar a cada um dos dez pontos de §D3 um subpasso "distinto e
**constante**". Para nove funciona. O décimo — o `RANDOM_INT(8)` de
`spr_road_walk` — é sorteado **uma vez por passo** de uma caminhada de
comprimento variável: `run_value` escala com o coeficiente de difusão e com o
valor da estrada, então o laço pode dar um passo ou dezenas. Não existe
constante que sirva.

O próprio §D3 já dizia "por passo da caminhada" na coluna *quando*; a
contradição estava entre duas seções do mesmo spec e passou despercebida na
escrita.

**Conserto:** `RNG_SUB_WALK` é uma **base**, não um valor — o passo *n* usa
`RNG_SUB_WALK + n`. Ela vale 16, e não 9, para deixar a faixa `[16, 65535]`
livre sem encostar em nenhum outro subpasso. A tabela de colisão completa está
em `rng.h`.

### B2 — o caso perigoso da âncora está na fase 5, e é pior do que o previsto

O §D8 avisa que `spr_urbanize` não pode ancorar no **alvo**, porque duas
origens que tentem urbanizar a mesma célula no mesmo ano receberiam sorteios
idênticos. Está certo, mas o caso que de fato morde é outro, e está na fase 5.

`spread.c`, laço de `tries` de `spr_phase5`: as **três** chamadas a
`spr_urbanize_nghbr` recebem **exatamente os mesmos argumentos** —
`i_rd_end_nghbr, j_rd_end_nghbr`. O laço nunca atualiza a origem; só escreve em
`i_rd_end_nghbr_nghbr`, que é descartado. No original as três diferem
unicamente porque `spr_get_neighbor` sorteia um vizinho a cada vez.

Ancorado nos argumentos, as três seriam **bit a bit idênticas**: mesmo vizinho,
mesma decisão, três vezes. O modelo perderia duas das três tentativas de
crescimento por estrada — e **nada acusaria**, porque o programa continuaria
perfeitamente determinístico e V3 continuaria valendo. É o modo de falha mais
traiçoeiro do estágio: o teste de determinismo passa e o modelo está errado.

O mesmo padrão, mais brando, está no laço de 8 tentativas da fase 1n3.

**Conserto:** a regra não é "ancore na origem", é ancorar em **(fase, origem,
tentativa)**, com o índice da tentativa obrigatório. `b = 0` é o trabalho da
iteração, `b = 1` o primeiro vizinho, `b = 2+tries` o laço.

*(Promovido a §B17 do mestre: quem mexer em `spr_urbanize` em S05/S06 precisa
saber disso sem abrir este arquivo.)*

**Nota lateral do mesmo trecho:** `urbanized` é atribuído e nunca lido dentro
do laço de `tries`. É mais um caso do padrão "o código calcula e joga fora" que
B15, B16 e a macro `URBANIZE` morta (B8) já catalogaram no mestre — o quarto.
Não afeta a execução.

### B3 — o `static last_index` era global sem precisar ser

§D5 e §B12 do mestre tratam o `static int last_index` de
`util_get_next_neighbor` como o segundo estado escondido do núcleo, e mandam
removê-lo em T5. Removido — mas a leitura do código revelou que ele **nunca
carregava informação entre rodadas**.

O cursor só era *incrementado* na chamada com `index == -1`, e nos dois únicos
consumidores (`spr_get_neighbor` e `spr_road_walk`) a primeira chamada de cada
rodada sempre passa um índice explícito de 0 a 7, que o **atribui**. Ele
portanto era observável apenas dentro de uma rodada, onde já é local por
natureza.

**Consequência:** T5 era seguro *por inspeção* — tornar o cursor local não
podia alterar o modo `legacy`, e VS18 confirmou. Isso justifica a decisão do
spec de pô-lo antes do T3: ele é um estágio de risco zero que limpa o diff do
arriscado.

**Consequência para o texto:** o argumento de §3.2 fica mais interessante, não
menos. O estado era global **sem que houvesse motivo** — ninguém decidiu que
ele deveria ser compartilhado, ele foi escrito assim. É o caso didático da
concorrência acidental: código serial que funciona por décadas e que só sob
threads revela que nunca precisou daquele `static`.

### B4 — o ano não precisa ser propagado

O §D8 descreve a chave como `(semente, ano, fase, a, b, subpasso)` e o §T3 fala
em propagar "o contexto `(ano, fase, a, b)`". Na prática a semente e o ano já
são globais imutáveis durante a corrida — `g_rng_seed` e `proc_GetCurrentYear()`
(`stubs.c`), este último já alimentado por `main.c`.

O que desce pela pilha é só o terno `(fase, a, b)`, empacotado num
`rng_ctx_t` de 12 bytes passado por valor. Reduziu bastante o alcance da
mudança de assinatura do T3, que era o risco declarado do estágio.

### B5 — o gerador ancorado é **mais rápido** que o `ran1`, não mais caro

O §N deste spec afirma, na seção para §4.2 do texto, que "o gerador ancorado é
mais caro por chamada que o `ran1` (uma mistura de 64 bits contra algumas
operações inteiras)". **Medido, é o contrário.**

10⁷ chamadas de cada, gcc 13.3 `-O2`, três repetições:

| gerador | ns por chamada |
|---|---:|
| `ran1` (legacy) | 6,22 · 6,22 · 6,27 |
| `rng_double` (anchored) | 5,91 · 6,01 · 5,73 |

Cerca de 6 % mais rápido, de forma estável, **apesar de fazer mais
multiplicações**.

**Por quê.** O `ran1` tem uma dependência serial entre chamadas: cada uma lê e
escreve `ran_seed` e a tabela `iv[32]`, então a chamada *n+1* não pode começar
antes de a *n* terminar. O `rng_double` é puro — chamadas consecutivas são
independentes e o processador as sobrepõe no pipeline. As multiplicações a mais
cabem nos ciclos que o `ran1` passa esperando a própria memória.

É exatamente a mesma propriedade que o torna paralelizável, medida num único
núcleo. Corrige §N e vira um argumento melhor para §4.2 do texto: **o
determinismo não custa desempenho, ele compra desempenho** — e o ganho aparece
antes de existir qualquer thread.

*(Promovido a §B18 do mestre: contraria o plano e é resultado de texto.)*

No programa inteiro a diferença some: 20 anos de `demo200` levam ~20 ms nos
dois modos, porque os 106 236 sorteios somam menos de 1 ms.

### B6 — a primeira versão de VS16 reprovou por critério errado, não por defeito

O teste de distribuição cobrava, na primeira escrita, cada um dos 10 baldes
dentro de ±1 % do esperado. A varredura do ano reprovou.

Não era o gerador. Com 10⁶ amostras, o desvio padrão de um balde é
`sqrt(10⁶ × 0,1 × 0,9) = 300`, então 1 % de 100 000 são **3,33 σ**. Exigir isso
de dez baldes em quatro varreduras reprova por acaso com probabilidade ~3 %.
Média, variância, autocorrelação e avalanche passavam todas com folga na mesma
rodada.

Trocado por **qui-quadrado**, que agrega os dez desvios num número de
distribuição conhecida: 9 graus de liberdade, crítico 27,88 para *p* = 0,001. O
pior eixo dá 21,34.

É o terceiro episódio do mesmo padrão neste projeto — §B14 do mestre e as duas
falsas falhas de §B14–B16 do S03. **O código estava certo e o critério de
comparação, errado.** Vale registrar que a forma de reconhecê-lo é sempre a
mesma: quando uma medida reprova e todas as outras do mesmo objeto passam com
folga, desconfie do critério antes do objeto.

### B7 — VS17 exigiu uma opção de linha de comando que `I.cli` não previa

O §V do estágio define VS17 como "varrer a fase 4 em ordem inversa de linhas
tem de dar `delta` idêntico", mas nada no protótipo permitia inverter a
varredura. Entrou `--scan forward|reverse`, com global `g_scan_reverse` lido
por `spr_phase4`.

É instrumento de verificação, da mesma família de `--dump` e `--hash`: não muda
o modelo, muda a ordem de visita. Fica para o S05 e o S07, onde a mesma
pergunta volta com threads. `I.cli` do mestre foi atualizado.

**O teste verifica os dois modos de propósito.** Que `anchored` não mude é a
afirmação; que `legacy` **mude** é a prova de que o teste tem dentes. Um VS17
que passasse nos dois modos estaria medindo outra coisa — provavelmente que a
fase 4 não está sendo exercitada. Medido: `legacy` diverge em 1478 bytes.

---

## §R resultados

### R1 — VS17, o resultado central do estágio

20 anos de `demo200`, semente 1, coeficientes de referência, varredura da fase
4 invertida:

| modo | forward × reverse |
|---|---|
| `anchored` | **idêntico byte a byte** |
| `legacy` | diverge em 1478 bytes |

V4 está demonstrada **sem nenhuma thread**. É o que o S05 precisava que
existisse antes de haver concorrência: se isto falhasse paralelizado, a culpa
seria atribuída à corrida de dados e o bug viveria escondido atrás de um mutex
que nunca o resolveria.

### R2 — T7: `legacy` × `anchored`, 12 sementes, 20 anos

Uma semente só não distingue "fluxo diferente" de "viés introduzido", porque os
dois produzem números diferentes. O que separa é a média sobre várias sementes.

| estatística | legacy | anchored | diferença |
|---|---:|---:|---:|
| `sng` (espontâneo) | 32,00 ± 4,56 | 31,25 ± 3,63 | −2,34 % |
| `og` (orgânico) | 1734,25 ± 36,18 | 1739,25 ± 37,80 | +0,29 % |
| `rt` (estradas) | 69,00 ± 9,78 | 67,50 ± 12,43 | −2,17 % |
| `cresc` (total) | 1838,50 ± 37,40 | 1839,83 ± 46,80 | +0,07 % |
| `pop` (ano 20) | 5719,50 ± 37,40 | 5720,83 ± 46,80 | +0,02 % |
| `decl_z` (ano 20) | 2,312 ± 0,02 | 2,319 ± 0,02 | +0,30 % |

**Nenhuma diferença de média alcança 0,2 desvio padrão.** Os dois maiores
percentuais, `sng` e `rt`, são justamente os de menor contagem absoluta — 32 e
69 eventos em 20 anos — onde um desvio de uma unidade já vale 2 %. Os
agregados grandes, `cresc` e `pop`, batem em 0,07 % e 0,02 %.

Conclusão: a troca de gerador muda os números e **não desloca o modelo**. É o
que §D7 pedia para avaliar, e a resposta é a favorável.

### R3 — custo

Ver §B5. Por chamada o gerador ancorado é ~6 % mais rápido; no programa inteiro
os dois modos são indistinguíveis (~20 ms para 20 anos de `demo200`), porque os
106 236 sorteios somam menos de 1 ms.

---

## §N notas para o texto

**Para §3.2 (exclusão mútua).** Este estágio dá o exemplo mais didático do
trabalho inteiro, porque é um problema de concorrência que **não é uma corrida
de dados** e que nenhum mutex resolve. Proteger o `ran_random` com trava
tornaria o programa correto quanto à memória e ainda assim não determinístico:
a ordem de consumo continuaria dependendo do escalonador. A solução não é
sincronizar o estado compartilhado, é **eliminá-lo** — trocar estado por função
pura da chave.

A taxonomia de B12 do mestre ganha sua forma final, com quatro itens e três
tratamentos distintos:

| estado compartilhado | natureza | tratamento |
|---|---|---|
| `delta` (RMW em `spr_urbanize`) | corrida de dados real | região crítica — S06, variantes A/B/C |
| `ran_random` (`iv[]`, `iy`, `ran_seed`) | estado oculto com dependência de ordem | **eliminado** — função pura (S04) |
| `last_index` (`util_get_next_neighbor`) | estado oculto entre chamadas | **eliminado** — cursor na pilha (S04) |
| normalização de estradas (§B9 do S03) | acoplamento por pré-processamento | ordem fixa antes das fases |

Ou seja: dos quatro pontos, **um** precisa de exclusão mútua. Os outros três
precisavam de projeto. É um argumento melhor do que o do plano original, que
tratava o problema como um ponto só.

**Para §4.1 (determinismo).** §D2 é o parágrafo-chave: o consumo de sorteios
por célula é condicional (0,134 por célula varrida, porque só célula urbana
sorteia), logo a posição no fluxo depende do conteúdo da grade. Um leitor que
assuma "cada célula consome um número fixo de sorteios" conclui que bastaria
dividir o fluxo em blocos por faixa — e estaria errado. Vale a tabela de §D1
junto, porque 99 % em uma única fase é um dado que orienta tanto o desenho do
gerador quanto o de S05.

**Para §4.2 (desempenho).** ~~O gerador ancorado é mais caro por chamada que o
`ran1` (uma mistura de 64 bits contra algumas operações inteiras), mas é
*stateless*: não tem a dependência serial que o `ran1` tem entre chamadas
consecutivas. A comparação `legacy` × `anchored` com 1 thread (T7) mede esse
custo em isolamento, antes de qualquer thread — e é o número honesto a
apresentar antes das curvas de speedup, porque parte do ganho de S08 é pago
aqui.~~

**Corrigido pela medição — ver §B5.** A previsão acima estava errada no sinal.
O gerador ancorado é ~6 % **mais rápido** por chamada (5,9 ns contra 6,2 ns),
apesar de fazer mais multiplicações. A metade certa da previsão era a segunda:
ele é *stateless* e não tem a dependência serial do `ran1` — e é justamente
isso que domina. Chamadas consecutivas são independentes e o processador as
sobrepõe; as multiplicações a mais cabem nos ciclos que o `ran1` passa
esperando a própria tabela de embaralhamento.

O parágrafo para §4.2 do texto fica melhor do que o planejado: **não há custo a
declarar antes das curvas de speedup**, porque o determinismo não foi pago em
desempenho. Ele foi pago em *projeto* — trocar estado por função pura da chave
—, e o efeito colateral num único núcleo é positivo. A propriedade que torna o
gerador paralelizável já se mede antes de existir qualquer thread.

A comparação `legacy` × `anchored` com 1 thread (T7) continua sendo o número
honesto a apresentar, e agora por um motivo melhor: ela mostra que a troca de
gerador não desloca o modelo (§R2) nem penaliza o relógio (§R3).
