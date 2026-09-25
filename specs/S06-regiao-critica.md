# S06 — a região crítica: variantes B e C, e a varredura que some

**Estágio:** S06 · **Status:** concluído (20/09) · **Dia no plano:** 19/09 (escrito e executado em 20/09)
**Mestre:** `../SPEC.md` · **Cita:** V3, V5, V6, V11, V15
**Alimenta o texto:** §3.2 (exclusão mútua), §3.3 (mecanismos) e §4.2 (desempenho)

**O recorte deste estágio foi arbitrado por medição, e a medição reordenou os
candidatos que o mestre listou.** O §T do mestre punha o pool persistente em
primeiro lugar ("o maior ganho disponível e o menos arriscado") e avisava, por
§B22, que o ganho das variantes B e C "pode ser pequeno se a banda de memória
já for o gargalo" — recomendando **medir antes de implementar**. Foi o que se
fez, com `harness/probe-s06.sh`, e o resultado inverte a ordem: a trava global
custa **+126 %** na fase 4 com 8 threads, e o pool custa 6 %.

A inversão tem uma causa só, e ela é o achado metodológico do estágio: **o
custo da região crítica é invisível em toda entrada que o projeto tinha até
ontem.** Ver §D1 e §D3.

---

## §G objetivo do estágio

Cumprir V6 — as três variantes da região crítica selecionáveis em tempo de
execução pela mesma compilação — e **medir o que cada uma custa** numa entrada
que consiga mostrar a diferença.

E, de passagem, eliminar a varredura `O(N)` mais barata de eliminar que sobrou
do S05: a contagem de população, que não precisa ser paralelizada porque pode
simplesmente deixar de existir (§D6).

Este é o estágio em que o trabalho deixa de ser "fazer rodar em N threads" e
passa a ser "escolher entre mecanismos de sincronização com número na mão" —
que é o assunto declarado do TCC (§G do mestre: "tratando explicitamente os
problemas de exclusão mútua").

## §S escopo

**Entra:** a interface única que as três variantes implementam (§D4); a
**variante B** (sharding de locks) e a **variante C** (CAS atômico) atrás dela;
a fusão da contagem de população no laço do merge (§D6); a medição comparativa
das três variantes na grade ladrilhada; VS24–VS27.

**Não entra**, e as duas exclusões são decisões com número, não omissões:

- **O pool persistente.** Custa 6,10 ms de um ano de 101 ms a 2000×2000 com 8
  threads — 6 %. É o candidato nº 1 do §T do mestre, e a medição o rebaixou:
  ele só domina em grade pequena (5,71 ms de 20,79 ms no `demo200`, 27 %), e
  V15 já proíbe afirmação de desempenho sobre grade pequena. Mexe no ciclo de
  vida das threads, o que obriga a refazer valgrind e TSan. Fica como candidato
  do S08 ou trabalho futuro declarado. Ver §D8.
- **A montagem da lista de crescimento da fase 5.** 13,33 ms, 13 % do ano. É o
  maior pedaço serial que sobra e é paralelizável — faixas com listas parciais
  concatenadas na ordem do índice da faixa, que reproduz exatamente a ordem
  serial. Mas a ordem da lista alimenta `growth_index`, então errar a
  concatenação **muda o modelo em silêncio**, e exige duas passadas (contar,
  prefixar, preencher). É a tarefa arriscada e fica declarada, não feita. Ver
  §D8.

**Não muda nenhuma regra do modelo.** As três variantes têm de produzir o mesmo
`z`, byte a byte, entre si e contra o serial. Uma variante que mude a saída não
é mais rápida: é errada.

---

## §D entradas conhecidas

Levantamento de 20/09, medido no código real antes de escrever este spec (§R1
do mestre). A sonda está versionada em `harness/probe-s06.sh` e é
reexecutável; a entrada que ela usa se reconstrói com `harness/mktiled.sh 10`.

Todos os números são mediana de 5, `clock_gettime(CLOCK_MONOTONIC)`, I/O fora
da região medida (V11). **A variação entre execuções da mesma configuração é
de 10 a 20 %** — o que importa registrar aqui, porque é da ordem de grandeza
de um achado do S05 que não sobreviveu (§D7).

### D1 — a entrada que faltava, e ela custou 40 linhas de `awk`

§B20 do mestre registra a dívida desde 19/09: o gerador artificial não produz
cidade. A 2000×2000 e 5 anos a fração urbana **arredonda para 0,00 %** — são
162 urbanizações tentadas em 20 milhões de células; o `demo200` começa com
**9,70 %**. Como a fase 4 curto-circuita em célula não urbana, medir perfil em
grade artificial grande mede uma grade *vazia*, não uma grade *grande*. §B24
estreitou o problema — as outras três varreduras fazem o mesmo trabalho por
pixel numa grade vazia — mas não o quitou.

A alternativa barata estava levantada desde o S05 e **nunca havia sido
testada**: em vez de escrever um gerador que produza cidade, repetir a cidade
que já existe. Está testada, funciona, e é `harness/mktiled.sh`:

| | fração urbana inicial |
|---|---|
| `demo200` 200×200 | 9,70 % |
| ladrilhado 10×10, 2000×2000 | **9,70 %** |

Idêntica, e não aproximada: ladrilhar é uma operação exata sobre a grade. **E
V3 continua valendo nela** — 1 e 8 threads dão o mesmo `z` byte a byte,
conferido antes de qualquer número deste spec ser colhido.

**O artefato conhecido é real e está declarado:** as estradas não se conectam
atravessando a costura entre ladrilhos. Isso afeta o **modelo** — a fase 5
cresce ao longo de estradas — e não o **perfil**. Quanto vale o afetado: a
caminhada pela estrada custa **0,15 ms de 14,08** da fase 5, ou seja 1 % dela e
0,1 % do ano (§D5). Para medir tempo, serve; para afirmar comportamento
urbano, não serve, e não é para isso que foi feito.

Isso **reduz** a dívida de §B20 sem quitá-la. O que o ladrilho dá é fração
urbana realista em grade grande, que é o que faltava para ver a região crítica.
O que ele não dá é uma cidade *diferente* da do `demo200` — para variar a
entrada, o `testgen` de T7 continua devido.

### D2 — onde está o tempo agora, na única entrada honesta

2000×2000 ladrilhado, 5 anos, variante A, mediana de 5. A coluna que interessa
é a de 8 threads, porque é onde o estágio vai trabalhar:

| etapa | 1 thread | 8 threads | % do ano a 8 | estado |
|---|---:|---:|---:|---|
| **fase 4** | 80,75 | **32,66** | **32,3 %** | paralela, **presa na trava** (§D3) |
| filtros | 31,85 | 13,56 | 13,4 % | paralela |
| **f5: montar a lista** | 13,93 | **13,33** | **13,2 %** | **serial**, `O(N)` (§D5) |
| **pop** | 12,51 | **12,53** | **12,4 %** | **serial**, `O(N)` (§D6) |
| merge | 22,24 | 10,88 | 10,8 % | paralela |
| zerar `delta` | 19,87 | 10,38 | 10,3 % | paralela |
| **pool** | 0,00 | **6,10** | **6,0 %** | custo puro (§D8) |
| f5: caminhada | 0,15 | 0,96 | 0,9 % | serial **por construção** |
| fase 1n3 | 0,08 | 0,78 | 0,8 % | serial **por construção** |
| **total** | **183,80** | **101,15** | | **1,82×** |

Três leituras que mudam o que o estágio faz:

1. **A fase 4 ainda é um terço do ano com 8 threads**, e não porque a grade é
   grande: porque a trava a segura (§D3). Sem trava ela cai para 14,63 ms.
2. **A parte "sequencial por construção" da fase 5 é 1 % dela.** Os outros
   99 % são a varredura `O(N)` escondida. O argumento do S05 — "as fases 1n3 e
   5 são sequenciais por construção" — continua verdadeiro e virou quase
   irrelevante: ele se aplica a 0,96 ms de 14,29.
3. **`pop` custa o mesmo com 1 e com 8 threads** (12,51 → 12,53), porque é
   serial. Somada à lista da fase 5, são **25,9 ms, 26 % do ano**, em duas
   varreduras que ninguém paralelizou.

O endereçável pelo recorte deste estágio é ~18 ms da fase 4 (§D3) + 12,5 ms da
`pop` (§D6) = **~30 ms de 101**, que levaria o ano a ~70 ms e o ganho de 1,82×
para ~2,6×.

### D3 — a trava global é o gargalo da fase 4, e só a fração urbana real mostra isso

`--variant A` contra `--variant none` — o **mesmo binário** sem trava nenhuma,
que é portanto o **piso teórico** de B e de C. Fase 4, em ms:

**2000×2000 ladrilhado (9,70 % urbano):**

| threads | A | none | custo da trava | ganho de A |
|---:|---:|---:|---:|---:|
| 1 | 80,94 | 80,16 | +1 % | 1,00× |
| 2 | 46,30 | 41,14 | +13 % | 1,75× |
| 4 | 29,76 | 22,26 | +34 % | 2,72× |
| 8 | **33,02** | **14,63** | **+126 %** | **2,45×** |

**2000×2000 artificial (0,00 % urbano) — a mesma medida, na entrada antiga:**

| threads | A | none | custo da trava |
|---:|---:|---:|---:|
| 8 | 4,48 | 4,51 | **−1 %** |

Com 1 thread a trava já é no-op (`par.c` só a ativa com `nthreads > 1`), então
a primeira linha mede o ruído: ±1–2 %.

**O que a tabela diz.** A trava global não é um custo marginal: com 8 threads
ela **mais que dobra** a fase 4. Pior, ela inverte o sinal da escalada — de 4
para 8 threads a fase 4 com trava **piora** (29,76 → 33,02) enquanto sem trava
continua acelerando (22,26 → 14,63). Sem trava a fase 4 escala 5,48×; com
trava, satura em 2,72× e regride. É contenção clássica, e é o alvo de B e C.

**E o que a tabela diz sobre a metodologia — que é o achado maior.** Na grade
artificial o mesmo experimento acusa **−1 %**, isto é, nada. A diferença não é
a máquina nem o tamanho: é que a trava mal é tomada.

| entrada | tomadas da trava | células varridas | tomadas/célula |
|---|---:|---:|---:|
| ladrilhado, 5 anos | 52 163 | 20 000 000 | 0,002608 |
| **artificial, 5 anos** | **162** | 20 000 000 | **0,000008** |
| `demo200`, 20 anos | 2 582 | 800 000 | 0,003227 |

São **325 vezes menos** tomadas na grade artificial, pelo motivo que §B20 já
dizia: sem cidade, a fase 4 curto-circuita e nunca chega à região crítica. A
entrada antiga não mede pouco o gargalo — ela **não o contém**.

E o `demo200`, que tem a fração urbana certa, também não servia: a proporção
está lá (0,0032 tomadas por célula, a maior das três), mas a grade é pequena
demais para que o efeito apareça acima do pool. Medido, o custo da trava no
`demo200` com 8 threads é **+6 %**, contra +126 % no ladrilhado.

**Então o gargalo deste estágio exigia as duas coisas ao mesmo tempo — fração
urbana realista E grade grande — e nenhuma entrada do projeto tinha as duas até
§D1.** É material de primeira para o texto: a escolha do dado de entrada
determina qual gargalo o experimento é capaz de enxergar. Um projeto que só
tivesse medido no `demo200` teria concluído, com medição legítima, que a trava
custa 6 % e que B e C não valem o esforço.

### D4 — as três variantes não cabem na interface de hoje, e o motivo é conceitual

`par.h` oferece hoje `par_lock()` / `par_unlock()`, e `spr_urbanize` as usa em
torno da cascata inteira. A interface exprime **"entrar numa região crítica"**.

- **A (mutex global)** e **B (sharding)** são a mesma ideia com granularidade
  diferente: B troca um mutex por um vetor de mutexes indexado pela célula
  alvo. `par_lock(off)` ganha um argumento e nada mais muda.
- **C (CAS)** não é uma região crítica. Não há entrada nem saída: há uma
  operação atômica que **ou toma a célula, ou descobre que alguém a tomou**. Não
  existe `par_lock` que exprima isso.

Forçar C na interface de trava daria um `par_lock` no-op e um CAS solto no meio
de `spr_urbanize`, com o leitor tendo de adivinhar quais linhas estão
protegidas em qual variante. **A interface tem de ser a da operação, não a do
mecanismo:**

```c
/* le delta[off] para decidir a cascata. A/B: leitura simples, ja sob trava.
   C: __atomic_load_n, relaxed — e so uma dica, quem decide e o commit. */
PIXEL par_cell_read   (GRID_P delta, int off);

/* tenta gravar. A/B: grava e devolve TRUE, porque a trava garante.
   C: CAS 0 -> value; FALSE quer dizer "outra thread chegou primeiro". */
bool  par_cell_commit (GRID_P delta, int off, PIXEL value);

/* A: mutex global. B: mutex[off % PAR_SHARDS]. C: no-op. */
void  par_cell_enter  (int off);
void  par_cell_leave  (int off);
```

E `spr_urbanize` ganha **um** ramo novo, morto sob A e B: o `commit` que falha,
que conta `delta_failure`. Sob A e B ele nunca executa — a trava já garantiu a
exclusão —, e sob C é o caminho normal da disputa perdida.

**§B4 do mestre vale aqui e é a armadilha a não repetir:** `PIXEL` é `long`, 8
bytes. O trecho da variante C no plano original usa `unsigned char expected =
0` e **está errado**. O CAS opera sobre `PIXEL`. E o falso compartilhamento é
de 8 células por linha de cache de 64 B, não 64 — o que importa para escolher
`PAR_SHARDS` em B.

### D5 — a variante C preserva os contadores, e o argumento é o de §B21

§B21 do mestre refinou V3: com a variante A, `sucesso` e `já era urbana` são
exatos, e só a **soma** de `já crescida` + `declividade` + `área excluída` é
invariante — a repartição entre as três depende de quem venceu cada disputa.

A variante C muda a ordem em que a disputa é descoberta: sob A, quem perde é
barrado na leitura de `delta` e nunca avalia declividade; sob C, quem perde
**avalia tudo** e só descobre a derrota no CAS. É preciso verificar que as
invariantes de §B21 sobrevivem, e sobrevivem:

| quantidade | por quê continua exata sob C |
|---|---|
| `sucesso` | só o vencedor do CAS escreve, e escreve uma vez |
| `já era urbana (z)` | depende só de `z`, imutável durante as fases (V2) |
| soma das três reprovas | o conjunto de tentativas é determinístico; o perdedor do CAS é contado **uma vez**, em `delta_failure` |

Os sorteios não entram na conta porque são função pura da chave da **origem**
(S04): o perdedor recebe o mesmo par de sorteios que receberia sozinho. Ele
apenas os desperdiça, o que é custo e não erro.

**O que muda é a repartição** — sob C, uma tentativa que sob A teria sido
`delta_failure` pode virar `slope_failure`, porque agora ela chega a avaliar a
declividade. Isso **já não era invariante** (§B21), e é exatamente por isso que
VS20 foi corrigida durante o S05 para cobrar a soma. Se a correção não
existisse, C reprovaria numa verificação errada — armadilha 4 do HANDOFF, pela
quarta vez no projeto.

**O serial continua bit a bit idêntico** nas três variantes, e é o que amarra
tudo: com 1 thread não há trava (A, B) e o CAS nunca falha (C), então a cascata
executa na mesma ordem e produz a mesma saída. VS21 e V7 seguem intactos.

### D6 — a contagem de população não precisa ser paralelizada: precisa sumir

`*pop = util_count_pixels (total_pixels, g_z, GE, PHASE0G)`, no fim de
`spr_spread`, é a quinta varredura `O(N)` que §B24 descobriu. Custa **12,53 ms,
12,4 % do ano** com 8 threads, e é serial.

O caminho óbvio é paralelizá-la como o merge: faixa, acumulador, redução. Mas
há um caminho melhor, e ele sai de olhar os dois laços juntos:

```c
/* o merge, hoje: */
for (i = w->pix_lo; i < w->pix_hi; i++)
  if ((z[i] == 0) && (delta[i] > 0)) { ...; z[i] = delta[i]; ... }

/* a pop, depois, em outra passada sobre a MESMA grade: */
for (i = 0; i < total_pixels; i++) if (z[i] >= PHASE0G) count++;
```

O merge já percorre a grade inteira, já tem `z[i]` em registrador, e já escreve
o valor final de `z[i]`. **Contar ali custa uma comparação por pixel e elimina
uma passada inteira de leitura de 32 MB.** A contagem por faixa somada sobre as
faixas é exatamente a contagem global, porque as faixas particionam
`[0, total_pixels)` sem buraco e sem sobreposição.

Não precisa nem de barreira nova: cada worker lê só a própria faixa, que ele
mesmo acabou de escrever — o mesmo argumento que já justifica não haver
barreira entre os dois filtros.

É a melhor relação ganho/risco do estágio: 12 % do ano por uma linha dentro de
um laço que já existe. E §B22 explica por que o ganho é maior do que
paralelizá-la daria: as varreduras são *memory-bound*, então a passada que mais
compensa é a que **não acontece**.

### D7 — §B23 do mestre não se reproduz, e isso precisa ser dito antes do texto

§B23 é um dos três achados que o HANDOFF manda ler "antes de escrever qualquer
coisa sobre desempenho". Ele afirma: paralelizar as varreduras deixa as fases
**seriais** mais lentas — fase 5 +45 %, contagem de população +43 %, de 1 para
8 threads a 2000×2000 — e oferece `pop` como o **controle limpo**, por ser
medida fora de qualquer barreira, depois do join.

Remedido em 20/09, com o **mesmo instrumento** (`harness/probe-s05-threads.sh`,
sem modificação), na **mesma grade artificial**, mediana de 7:

| etapa | §B23 (19/09) | remedido (20/09) |
|---|---|---|
| `pop`, 1 → 8 threads | 16,21 → 23,25 (**+43 %**) | 11,54 → 11,45 (**−1 %**) |
| fase 5, 1 → 8 threads | 23,52 → 34,22 (**+45 %**) | 14,42 → 15,60 (**+8 %**) |

E na grade **ladrilhada**, que é a entrada boa, `pop` vai de 12,51 a 12,53 —
plana. Em três grades (200×200, 1000×1000, 2000×2000) `pop` fica plana.

**§B23 cai pelo seu próprio critério:** o controle que ele elegeu é o que não
reproduz. O que sobra é a fase 5 subindo 8 %, num trecho de sub-milissegundo
por ano, e crescendo quando a grade encolhe (a 200×200 vai de 0,16 a 2,41 ms) —
assinatura de custo de barreira e de acordar thread, não de localidade de
cache.

**A causa provável do número original é carga de fundo na sessão.** Todos os
absolutos daquela sessão são 25–30 % mais altos que os de agora na mesma
configuração (1 thread, 2000×2000: total 162,41 contra 108,50), o que é
consistente com a máquina estar ocupada. A mediana de 5 ou 7 protege contra
oscilação **dentro** de uma sessão; não protege contra uma sessão inteira mais
lenta — e o efeito medido era justamente uma razão entre duas medidas tomadas
naquela sessão.

**Consequência.** §B22 (banda de memória) **se reproduz** e continua de pé:
zerar `delta` 1,93×, merge 1,91×, filtros 2,66× — a assinatura de saturação é a
mesma. §B23 não. Sobe para o mestre como §B25, qualificando §B23, e **a
recomendação é não usá-lo no texto**. Também retira a "segunda razão" que §B23
dava para o pool persistente e o argumento que ele dava para a lista da fase 5;
os dois continuam justificáveis, por tamanho (§D8), não por esse efeito.

É a quarta vez que o projeto tropeça no padrão da armadilha 4 do HANDOFF —
desta vez do lado contrário: não um critério errado reprovando código certo,
mas uma medição isolada virando achado documentado. **Achado de desempenho
precisa ser remedido em outra sessão antes de entrar no texto.**

### D8 — o que fica de fora, com o número que justifica

| candidato | custo medido a 8 threads | por que fica fora |
|---|---:|---|
| montagem da lista da fase 5 | 13,33 ms (13,2 %) | paralelizável e **arriscada**: a ordem alimenta `growth_index`, e exige contar → prefixar → preencher em duas passadas. Errar muda o modelo em silêncio |
| pool persistente | 6,10 ms (6,0 %) | mexe no ciclo de vida das threads (obriga valgrind e TSan de novo) por 6 % do ano; domina só em grade pequena, onde V15 proíbe afirmar desempenho |
| fase 1n3 e caminhada da fase 5 | 1,74 ms (1,7 %) | sequenciais **por construção**, e agora comprovadamente irrelevantes |

Os dois primeiros continuam registrados como dívida no HANDOFF. **A ordem entre
eles inverteu** em relação ao §T do mestre: a lista da fase 5 vale o dobro do
pool, e o pool deixou de ser "o maior ganho disponível" assim que a medição
saiu da grade pequena.

---

## §T tarefas

| id | st | tarefa | cita |
|----|----|--------|------|
| T1 | x | Fundir a contagem de população no laço do merge (§D6): o acumulador entra em `par_acc_t`, `util_count_pixels` sai de `spr_spread`. **Primeira** porque é independente das variantes e reexercita o padrão de redução do S05 — se VS21 quebrar aqui, a causa está cercada | V5, V3 |
| T2 | x | Interface única das três variantes em `par.h`/`par.c` (§D4): `par_cell_read`, `par_cell_commit`, `par_cell_enter`, `par_cell_leave`. A variante **A é reimplementada atrás dela** e não pode mudar de comportamento — VS21 é a rede | V6 |
| T3 | x | **Variante B** — sharding de locks, `--variant B`. Vetor de `PAR_SHARDS` mutexes indexado pela célula alvo; escolher `PAR_SHARDS` considerando o falso compartilhamento de §B4 (8 `PIXEL` por linha de cache) | V6 |
| T4 | x | **Variante C** — CAS atômico, `--variant C`. `__atomic_compare_exchange_n` sobre `PIXEL`/`long`, **não `unsigned char`** (§B4). O ramo de commit perdido conta `delta_failure` (§D5) | V6, V3 |
| T5 | x | Medir as quatro configurações (A, B, C, `none`) × {1,2,4,8} threads na grade ladrilhada, com `none` como piso. É a tabela de §4.2 do texto. `probe-s06.sh` ganha o eixo de variante | V11, V15 |
| T6 | ~ | `run_checks.sh` ganha VS24–VS27. `--variant` entra na matriz de verificação, como `--threads` entrou no S05 | V3, V6 |

Ordem: T1 → T2 → T3 → T4 → T5 → T6.

**Executada como T1 → T2 → T3 → T4 → T6 → T5**, e é o desvio que o `~` do T6
marca. O motivo é barato de explicar: o T5 custa cerca de 15 minutos de
sondagem, e medir uma variante que ainda não se sabe correta é gastar os 15
minutos duas vezes. Rodar a verificação antes fecha o risco pelo lado certo —
VS24–VS27 passaram antes de o primeiro número ser colhido, então a tabela de
§B4 mede três variantes que já se sabiam equivalentes na saída. Não há nada em
T5 de que T6 dependa, e a recíproca é a que importa.

**T1 antes de tudo pelo mesmo motivo que o S05 pôs T3 antes de T6**, e o S04
pôs T5 antes de T3: começar pela mudança que não toca em sincronização nenhuma.
Se a igualdade bit a bit contra o pré-S05 quebrar no T1, o problema é a fusão
do laço; se quebrar só a partir do T3, é a variante. Separar os dois custa uma
tarefa e economiza uma depuração.

**T3 antes de T4** porque B é o passo pequeno a partir de A — mesma estrutura,
índice diferente — e C é o que muda a forma da região crítica. Se a interface
de T2 estiver errada, B acusa antes e mais barato.

## §V verificação do estágio

VS1–VS23 continuam valendo. VS17 (`--scan reverse`) continua sendo o primeiro
instrumento de diagnóstico: ele responde "a ordem importa?" sem nenhuma thread
no caminho, e separa dependência de ordem de corrida de dados.

- **VS24** — **V3 sob as três variantes.** `--variant A`, `B` e `C` produzem
  dumps de `z` idênticos byte a byte entre si e contra `--threads 1`, para 1,
  2, 4 e 8 threads, em `--rng anchored`, no `demo200`. É VS19 com o eixo de
  variante. Uma variante mais rápida que mude a saída não é uma otimização.
- **VS25** — **as invariantes de §B21 valem para B e para C.** Não é repetição
  de VS20: a variante C **muda a ordem em que a disputa é descoberta** (§D5), e
  é preciso verificar que a soma continua exata.

  | quantidade | exigência |
  |---|---|
  | a tabela anual inteira (as sete estatísticas) | idêntica byte a byte |
  | `sucesso`, `já era urbana (z)` | idênticos |
  | `já crescida` + `declividade` + `área excluída` | **soma** idêntica |

  **Cobrar os cinco contadores idênticos seria errado**, e sob C mais ainda que
  sob A. Ver §B21 do mestre e §D5 acima.
- **VS26** — **ThreadSanitizer limpo em B e em C**, e continuando a **acusar**
  em `none`. É VS23 com o eixo de variante, e mantém as duas metades: uma
  verificação que passasse nos dois lados não estaria medindo nada. Sob C o
  TSan precisa reconhecer as operações atômicas — se ele acusar corrida sobre
  `delta` na variante C, ou o CAS não está atômico ou está na largura errada
  (§B4).
- **VS27** — **`--threads 1` continua reproduzindo bit a bit a saída pré-S05**,
  nos dois modos de RNG, com a fusão da `pop` no merge já feita. É VS21, e a
  T1 é a tarefa que mais o ameaça: mudar quem conta a população é mexer numa
  das sete estatísticas comparadas. As referências já existem em
  `tests/golden/` e não precisam ser recolhidas.

**Sobre medir T5.** A medição vale na grade **ladrilhada** (§D1), nunca na
artificial — §D3 mostra que a artificial acusa −1 % onde a boa acusa +126 %. E
V15 continua valendo: o que sai do T5 é a comparação **entre variantes**, não
afirmação de speedup do trabalho. Essa é o S08.

---

## §B achados do estágio

*(preenchido durante a execução, em ordem de descoberta — §R4 do mestre)*

### B1 — a fusão da `pop` no merge sai de graça sob 8 threads, e custa ~3 ms sob 1

T1 feito e medido com `harness/probe-s06-t1.sh`, que é instrumento novo e está
versionado. Na grade ladrilhada, 5 anos, mediana de 9, variante A:

| threads | `pop` que sumiu | o que o merge ganhou | ganho líquido |
|---:|---:|---:|---:|
| 1 | 13,02 | **+3,68** | 9,34 ms |
| 2 | 12,93 | +1,09 | 11,84 ms |
| 4 | 13,72 | +0,24 | 13,48 ms |
| 8 | 13,51 | **−0,07** | 13,58 ms |

A coluna do meio é a que não estava prevista em §D6. A comparação por pixel
acrescentada ao merge **custa ~3,7 ms com uma thread e zero com oito**, e a
explicação é §B22 do mestre: o merge é *memory-bound*: com oito threads
disputando a banda, a comparação a mais executa na sombra da espera por
memória e não aparece no relógio. Sob uma thread não há sombra em que se
esconder.

A consequência prática é que **o ganho do T1 cresce com o número de threads**
(9,34 → 13,58 ms) sem que nada nele tenha sido paralelizado. É o contrário do
que a intuição sugere e vale como material de §4.2.

No `demo200` o mesmo T1 vale 0,05 a 0,30 ms em 20 anos — V15 em ação: a grade
pequena não tem sobre o que mostrar.

### B2 — duas execuções em sequência não são comparáveis: a segunda é mais lenta

**E isto quase virou um achado inventado.** A primeira leitura do T1 foi pelo
total do ano e dizia que o T1 tinha **piorado** o programa em 4 e 8 threads
(+1,6 % e +5,7 %). O ganho procurado é ~13 ms de ~110, ou 12 %, e a variação
entre execuções neste projeto é de 10 a 20 % (§D): o efeito estava dentro do
ruído e o sinal era aleatório.

O que resolveu foi pôr um **controle** na sonda. O T1 não tocou em nada fora do
merge — zerar `delta`, 1n3, fase 4, fase 5 e filtros são byte a byte o mesmo
código nas duas árvores —, então a soma desses cinco (`resto`) tem de ser
idêntica nos dois binários, e o que ela acusar de diferença é a medida direta
da incerteza da sessão. Ela acusou, e não acusou ruído:

| threads | `resto`, ordem direta | `resto`, ordem invertida |
|---:|---:|---:|
| 2 | **+8,16** | −4,94 |
| 4 | **+11,28** | −6,49 |
| 8 | **+8,51** | **−19,47** |

**O sinal acompanha a ordem, não o binário.** Rodando `antes` primeiro, o
`depois` parece mais lento; invertendo, o `antes` parece mais lento. Em código
idêntico. O que se mede aí é a posição na sequência, não a árvore — e a 8
threads o artefato chega a 19 ms, quase 20 % do ano e mais que o efeito
inteiro que se queria medir.

Duas consequências, e a segunda é maior que este estágio:

1. **O estimador por total não serve para comparar duas árvores**, nem dentro
   da mesma sessão. O que serve é o estimador **local**: `pop` que sumiu menos
   o que o merge ganhou. Ele deu 9,34/11,84/13,48/13,58 numa ordem e
   10,24/13,34/12,72/13,61 na outra — estável, porque é diferença entre etapas
   e não razão entre totais.
2. **Isto é um mecanismo candidato para §B23.** O `probe-s05-threads.sh`
   percorre 1, 2, 4 e 8 threads *nessa ordem*, e §B23 concluiu que as etapas
   seriais pioram com mais threads — a `pop` +43 %, medida na última
   configuração da sequência. §B25 já derrubou §B23 e atribuiu o número a
   "carga de fundo"; o efeito de ordem medido aqui é mais específico que isso e
   está na direção certa. **Fica como hipótese, não como achado** — não foi
   medido contra §B23 diretamente, e §B25 existe exatamente para impedir que
   uma medição isolada vire afirmação. Quem for ao S08 tem aqui o que testar:
   percorrer a matriz de threads em ordem embaralhada e ver se o efeito some.

§B25 continua valendo por cima de B1: os números acima só entram no texto
depois de remedidos em outra sessão.

### B3 — `util_count_pixels` ficou sem chamador na árvore de trabalho

Efeito colateral do T1, registrado para não parecer esquecimento. A função
continua em `util.c`, portada e correta, e agora **ninguém a chama** em
`sleuth-par/` — era o único consumidor. Não foi removida: é parte da superfície
portada de `SLEUTH/utilities.c`, o `-Wall -Wextra` não reclama de função não
estática sem uso, e apagá-la afastaria a árvore de trabalho da referência
normativa sem ganho nenhum. Fica anotada aqui porque quem procurar "onde a
população é contada" vai achá-la primeiro, e ela é a resposta errada desde o
T1.

### B4 — B e C chegam as duas ao piso, e a trava era o que impedia a fase 4 de escalar

A tabela do T5. Grade ladrilhada 2000×2000, 5 anos, mediana de 5, **com a ordem
das variantes rodando a cada repetição** (§B2 — sem isso a primeira variante da
sequência ganharia de graça). `none` é o mesmo binário sem exclusão nenhuma: é
o piso, não uma quarta opção.

| threads | fase 4: A | B | C | none | ano: A | B | C | none |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 83,64 | 83,48 | 83,42 | 84,06 | 176,64 | 175,35 | 176,74 | 178,56 |
| 2 | 47,07 | 42,82 | 42,77 | 42,74 | 111,60 | 113,81 | 106,74 | 111,73 |
| 4 | 32,11 | 24,11 | 23,40 | 23,07 | 89,68 | 82,56 | 86,46 | 79,90 |
| 8 | **35,48** | **15,48** | **15,25** | 15,40 | **103,89** | **77,17** | **77,17** | 78,72 |
| **custo a 8** | **+130 %** | **+1 %** | **−1 %** | (piso) | **+32 %** | −2 % | −2 % | (piso) |

Três leituras:

1. **B e C eliminam o custo da trava por inteiro.** Não o reduzem: encostam no
   piso, dentro do ruído de ±1–2 % que a linha de 1 thread mede. O alvo do
   estágio era ~18 ms da fase 4 (§D2) e saíram 20.
2. **A não é só mais lenta: ela faz o programa parar de escalar.** De 4 para 8
   threads a fase 4 sob A **piora** (32,11 → 35,48) e o ano inteiro também
   (89,68 → 103,89), enquanto sob B e C os dois continuam caindo. A escalada de
   1 para 8 threads do ano inteiro é **1,70× sob A** e **2,27× sob B / 2,29×
   sob C**. Não é o trabalho que não escala — é a exclusão mútua escolhida.
3. **§D3 passou no teste de §B25.** O custo da trava foi medido em 20/09 (+126 %
   na fase 4 com 8 threads) e remedido **nesta sessão** (+130 %). Um achado de
   desempenho deste projeto sobreviveu a uma sessão diferente — o segundo
   depois de §B22, e agora sabe-se que §B23 não sobreviveria. As colunas de B
   e C, essas, são **desta sessão só** e ainda devem a remedição.

### B5 — B e C empatam, e o empate é o achado

As duas variantes que §D4 diz **não caberem na mesma abstração** — uma é região
crítica, a outra não é — custam a mesma coisa: 15,48 contra 15,25 ms, diferença
menor que o ruído. O CAS não comprou desempenho sobre o sharding.

Isso não esvazia §D4; inverte o que ele decide. **A escolha entre B e C não é
de desempenho, e portanto tem de ser feita nos outros eixos**, que são
assimétricos:

| | B (sharding) | C (CAS) |
|---|---|---|
| memória | 64 KB de mutexes, estáticos | zero |
| por tentativa | um par lock/unlock, sempre | uma carga relaxed + um CAS só quando vale |
| o que pode dar errado | `PAR_SHARDS` pequeno demais (§B6) | **a largura do CAS** (§B4 do mestre) |
| leitura do código | familiar: é A com outro índice | exige entender que a leitura é uma dica |

O modo de falha de C é pior que o de B, e é o que §B4 do mestre já avisava: um
CAS de `unsigned char` sobre uma célula de 8 bytes compara o byte menos
significativo e deixa sete desprotegidos — uma variante que *parece* funcionar
e não exclui nada. O de B é benigno: poucas travas custam desempenho e não
corretude. **Para o texto (§3.2), é o argumento mais honesto do estágio: o
mecanismo mais sofisticado não foi o mais rápido, e escolher entre eles é
escolher que erro se prefere correr o risco de cometer.**

### B6 — quantas travas B precisa, medido — e a guarda que prova o mecanismo

`PAR_SHARDS` é sobrescritível na compilação para que o número saia de medição e
não de argumento. Fase 4, 8 threads, grade ladrilhada, mediana de 5:

| `PAR_SHARDS` | fase 4 | sobre o piso |
|---:|---:|---:|
| **1** | **37,59** | **+146 %** |
| 8 | 18,18 | +19 % |
| 64 | 16,14 | +6 % |
| **1024** | **15,49** | **+1 %** |
| 16384 | 15,79 | +3 % |

**A primeira linha é a guarda, e é o que torna a tabela confiável.** Com uma
trava só, B *é* a variante A por construção — e custa o que A custa (+146 %
contra os +130 % de A, dentro do ruído). Se aquela linha tivesse dado o piso, o
mecanismo de B não seria o que o código diz que é: o índice estaria errado, ou
a trava não estaria sendo tomada, e as outras linhas não valeriam nada.

A curva é a de contenção contra granularidade, e ela **satura**: 1024 encosta
no piso e 16384 não melhora — 1 MB de mutexes começa a custar cache. 1024 é a
escolha, e agora com a curva atrás dela.

### B7 — a mudança de repartição que §D5 previu para C não se observa

§D5 previu que a variante C mudaria a repartição dos cinco contadores: sob A
quem perde a disputa é barrado na leitura de `delta` e é contado em
`já crescida`; sob C ele avalia a cascata inteira, então um perdedor que
reprove na declividade seria contado em `declividade`. Medido com
`harness/probe-s06-contadores.sh`, na grade ladrilhada, 3 execuções por
configuração:

| | sucesso | z_fail | já cresc. | declivid. | excluída | soma |
|---|---:|---:|---:|---:|---:|---:|
| A, B e C · 1 e 8 threads | 38 067 | 101 756 | 1 029 | 10 504 | 2 563 | 14 096 |

**Idêntico em tudo — as dezoito execuções dão a mesma linha.** No `demo200` há
variação (58/597 contra 60/595 entre 1 e 8 threads), mas ela é a de §B21, vem
da contagem de threads e é **a mesma nas três variantes**.

A explicação é que a disputa real é rara: são **dois eventos em 766** no
`demo200`, e o evento que distinguiria C exige mais que uma disputa — exige que
o perdedor passe pela leitura otimista (isto é, que os dois leiam 0 na janela
entre a leitura e o CAS, que são dois sorteios) e depois reprove na
declividade. Na grade grande a fronteira entre faixas é 2 linhas de 250, contra
2 de 25 no `demo200`, então a colisão é proporcionalmente ~10× mais rara ainda.

**§D5 continua certo no argumento e não se confirma no número, e as duas coisas
importam.** O que ele garante — que a soma das três reprovas sobrevive a C — é
o que VS25 cobra, e vale. O que ele previu de diferença visível não apareceu, e
**não se deve escrever no texto que "sob C a repartição muda"**: não foi
observado. Registrar "não se observou" e não "não existe" é a diferença que
§B25 pede.

Há um corolário de projeto: sob C a leitura relaxed **filtra quase todos os
perdedores antes do CAS**, então o ramo que §D4 criou para a disputa perdida é
quase morto até na variante que o exige. Ele continua sendo obrigatório —
retirá-lo é perder a corrida em silêncio —, mas o caminho caro de C quase nunca
executa, o que ajuda a explicar §B5.

---

## §N notas para o texto

**Para §3.2 (exclusão mútua) — o achado de interface.** §D4 é um parágrafo que
o plano original não previa: as três variantes **não cabem na mesma
abstração**. A e B são "entrar numa região crítica" com granularidade
diferente; C não é região crítica nenhuma, é uma operação que ou toma a célula
ou descobre que a perdeu. A interface honesta é a da operação, não a do
mecanismo — e quem tentar exprimir CAS como trava acaba com um `lock()` que não
tranca nada. Fecha bem com §3.2 do S04, onde a solução também não era
sincronizar.

**Para §3.3 (mecanismos) — a escala de dificuldade ganha um degrau.** A tabela
do S05 (filtros → zerar `delta` → merge → fase 4) mostrava que os mecanismos
não são intercambiáveis. Este estágio mostra que **dentro** do degrau mais alto
ainda há escolha, e que ela tem preço medido: 100 % das urbanizações
serializadas para proteger as ~4 % que caem na fronteira das faixas (§D3 do
S05), e a conta sai em +130 % na fase 4.

**E o degrau tem um segundo andar que a execução descobriu (§B5, §B6).** A
escolha dentro dele **não é entre "mais lento" e "mais rápido"**: B e C
empatam, as duas no piso. O que as separa é o modo de falha — B erra por
desempenho (poucas travas) e C erra por corretude (CAS na largura errada, §B4
do mestre) — e é disso que §3.3 deve tratar, porque é o que sobra quando o
desempenho não decide. A curva de §B6 é o material gráfico do capítulo: uma
trava +146 %, oito +19 %, sessenta e quatro +6 %, mil e vinte e quatro +1 %, e
depois satura. A granularidade da exclusão é um botão contínuo, e o texto pode
mostrá-lo girando.

**Para §4.2 (desempenho) — o achado metodológico, e é o melhor do estágio.**
§D3 é a seção a escrever com cuidado: o mesmo experimento, no mesmo binário, na
mesma máquina, dá **+126 %** numa entrada e **−1 %** na outra, e as duas
entradas têm 2000×2000 pixels. A diferença é a fração urbana, e o mecanismo é
conhecido desde §B20 — a fase 4 curto-circuita em célula não urbana e nunca
chega à região crítica. **A escolha do dado de entrada determina qual gargalo o
experimento é capaz de enxergar.** Um projeto que só tivesse medido no
`demo200` teria concluído, com medição legítima e honesta, que a trava custa
6 % e que B e C não valem o esforço.

**Para §4.2 também — o que não entra, e por quê.** §D7 tira §B23 do texto.
Custa um achado, e vale mais do que custa: mostra o critério em ação (§V12 do
mestre — nada é corrigido silenciosamente) e rende a regra que o projeto
aprendeu tarde, **achado de desempenho se remede em outra sessão antes de virar
afirmação**. §B22, que passou pelo mesmo teste e sobreviveu, fica mais forte
por isso.

**Sobre honestidade de escopo.** §S declara duas exclusões com número — 13,2 %
e 6,0 % do ano — em vez de as omitir. É a mesma escolha que o S03 fez ao
imprimir VS12 e VS13 como *adiadas* em vez de sumir com elas: dívida declarada
é dívida que ainda existe; dívida omitida vira esquecimento.

**Para §4.2 também — o que a execução acrescentou, e é metodologia outra vez.**
§B2 é irmão de §D3 e merece o mesmo cuidado. §D3 diz que a **entrada** decide
qual gargalo o experimento enxerga; §B2 diz que a **ordem das execuções** decide
o sinal do resultado. Medido: a mesma árvore, comparada contra si mesma, acusa
+8 ms ou −19 ms conforme qual binário roda primeiro — e a primeira leitura do
T1, feita pelo total do ano, concluiu que ele tinha piorado o programa. Os dois
achados têm a mesma forma e a mesma moral: **o experimento tem parâmetros que
não estão no código sendo medido**, e não declará-los é publicar um número sem
dizer do que ele é número. A consequência prática ficou no instrumento — a
sonda do T5 rotaciona a ordem das variantes a cada repetição, senão a variante
medida primeiro ganharia de graça.

**E uma linha sobre o que NÃO entra (§B7).** §D5 previu que a variante C
mudaria a repartição dos contadores. Mediu-se, e não se observou — em dezoito
execuções, nas duas grades. O argumento de §D5 continua correto e a invariante
que ele protege (a soma) vale; a diferença visível, não apareceu. **Não escreva
"sob C a repartição muda".** Escreva, se for o caso, que ela pode mudar e não
se observou mudar — que é a mesma disciplina de §B25, aplicada antes de o
número existir em vez de depois.
