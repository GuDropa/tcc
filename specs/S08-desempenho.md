# S08 — desempenho: o instrumento antes da tabela

**Estágio:** S08 · **Status:** a executar (escrito em 20/09) · **Dia no plano:** 23/09
**Mestre:** `../SPEC.md` · **Cita:** V11, V15
**Alimenta o texto:** §4.2 (desempenho) e §4.1 (o que uma medida sustenta)

**O recorte deste estágio foi arbitrado por medição, e a medição inverteu a
ordem.** O §T do mestre diz "S08 — Desempenho: grades grandes × {1,2,4,8}
threads × 3 variantes". Essa tabela é o produto do estágio e continua sendo — mas
a sondagem que precedeu este spec (`harness/probe-s08-ruido.sh`, §D) mostrou que
**o instrumento que a produziria ainda não sabe escolher um número**, e que a
verificação encarregada disso, escrita no S03 e adiada desde então, **especifica
o estimador errado**.

Então o estágio é, em ordem: **decidir o estimador, consertar o instrumento,
depois publicar a tabela.** Escrever o S08 como a linha do mestre o descreve
produziria uma tabela bonita cujo ruído é maior que várias das diferenças que ela
mostraria — que é exatamente o modo de falha de §B29.

Quarta vez que "sondar antes de catalogar" muda o estágio, depois de S04, S05 e
S06. Desta vez o que ele corrigiu não foi o escopo: foi uma **verificação já
escrita**, parada há cinco estágios esperando este.

---

## §G objetivo do estágio

Produzir a tabela de desempenho que o §4.2 do texto precisa — e, antes dela,
fazer o projeto saber **qual número da medição publicar**, que é uma pergunta que
ele ainda não tinha respondido com medida.

Fechar, de passagem, as duas últimas verificações adiadas do projeto (VS12 e
VS13) e o registro durável de baseline (T11), que estão pendentes desde o S03 e
são as três tarefas que o desvio daquele estágio mandou para cá.

O estágio fecha com a suíte em **zero adiadas** pela primeira vez.

## §S escopo

**Entra:** a remedição da sondagem de ruído (T0); `--repeat N` (T1, que é a T8 do
plano); VS12 corrigida (T2); VS13 repontada (T3); a tabela de §T (T4);
`harness/baseline.md` (T5).

**Não entra**, e as exclusões são decisões com número ao lado, não omissões:

- **O pool persistente.** Continua declarado fora, e este estágio acrescenta um
  argumento novo a favor dele sem puxá-lo para dentro — ver §D3. Mexe no ciclo de
  vida das threads e obriga a refazer valgrind e TSan, a cinco dias do prazo.
- **A montagem da lista da fase 5** (13 % do ano). Mesma razão do S06 (§D8 de
  lá): a ordem da lista alimenta `growth_index` e errar muda o modelo em
  silêncio.
- **O `testgen` que escale (T7).** E aqui há uma mudança: ele deixa de ser
  pré-requisito de VS13. Ver §D4 — o caminho ladrilhado já entrega o que VS13
  cobra, e o que sobra de T7 é variedade de modelo, não medição de desempenho.
  **T7 passa a trabalho futuro declarado**, não dívida do S08.
- **O texto.** O autor está escrevendo em paralelo. Não se encosta em
  `Esqueleto TCC.txt` nem em arquivo de texto do TCC.

**Não muda nenhuma regra do modelo.** A única mudança de comportamento prevista é
uma bandeira nova (`--repeat`) que não altera a simulação. Se este estágio
precisar mexer em `spread.c`, `par.c` ou `rng.c` para passar, o spec está errado.

---

## §D entradas conhecidas

Sondagem de 20/09, `harness/probe-s08-ruido.sh` (versionada), grade ladrilhada
2000×2000 com fração urbana real, 5 anos, `--variant A`. **A sonda rodou duas
vezes, com minutos de intervalo, e é a comparação entre as duas rodadas que
sustenta §D1 e §D2** — uma rodada só teria dado a resposta errada, e este spec
mostra onde.

### D1 — o ruído do total do ano não é deriva; é contaminação unilateral

§B29 do mestre mediu 4,6 % de espalhamento no total do ano com código byte a byte
idêntico, e deixou a regra "compare etapas, nunca totais". O que faltava era o
**mecanismo**, porque sem ele não dá para saber se mais repetição resolve.

A sonda repete a **mesma configuração** N vezes — não quatro variantes que por
acaso são o mesmo código, como §B29 — e imprime a série na ordem de execução. A
1 thread, 21 repetições, total do ano em ms:

```
180,68 167,07 205,61 177,17 203,90 169,87 190,62 168,15 168,84 170,64 168,33
166,64 164,37 166,36 166,14 170,90 171,11 168,96 182,80 203,80 169,99
```

Não é ruído simétrico em torno de um centro: é um **piso em ~166-171 com
excursões para cima**, algumas de 20 %. A distribuição é unilateral porque a
causa é: carga de fundo rouba tempo e nunca devolve.

**E não é deriva térmica, com prova limpa.** A coluna "primeira metade contra
segunda" da sonda trocou de sinal entre as duas rodadas:

| | 1 thread | 8 threads | `pool` a 8 threads |
|---|---:|---:|---:|
| rodada 1 (15 rep) | +1,9 % | +15,1 % | +90,5 % |
| rodada 2 (21 rep) | −1,0 % | −1,4 % | −24,3 % |

Deriva não troca de sinal entre duas execuções separadas por minutos. E na
rodada 1, que "derivava" +15,1 %, a **última** execução da série foi a mais
rápida (90,25 contra um máximo de 124,31) — deriva termina alto, contaminação
volta ao piso.

O que **reproduz** entre as duas rodadas é o piso:

| | rodada 1 | rodada 2 | distância |
|---|---:|---:|---:|
| mínimo do ano, 1 thread | 168,11 | 164,37 | 2,3 % |
| mínimo do ano, 8 threads | 90,16 | 88,53 | 1,8 % |

**Consequência:** a pergunta do S08 não é "quantas repetições", é "qual
estimador". Esse é §D2.

### D2 — o mínimo é o estimador que sobrevive ao pior caso

A sonda parte a série em grupos, na ordem de execução, e calcula cada estimador
por grupo. Espalhamento **entre grupos** mede reprodutibilidade — o estimador bom
concorda consigo mesmo, e nenhuma teoria sobre a causa é necessária para
escolher.

| | mín. | mediana | média |
|---|---:|---:|---:|
| 1 thread, grupos de 5 (rodada 1) | **1,3 %** | 2,0 % | — |
| 1 thread, grupos de 7 (rodada 2) | **1,6 %** | 7,5 % | 10,4 % |
| 8 threads, grupos de 5 (rodada 1) | **4,4 %** | **23,8 %** | — |
| 8 threads, grupos de 7 (rodada 2) | 2,8 % | 1,9 % | 3,0 % |
| **pior caso** | **4,4 %** | **23,8 %** | ≥ 10,4 % |

**Leia a tabela pela última linha, não pela média das outras.** A mediana passou
bem a 8 threads na rodada 2 (1,9 %, melhor que o mínimo) e foi a pior de todas na
rodada 1 (23,8 %). Ou seja: **a resposta a "qual estimador serve" mudou entre
duas rodadas da mesma sonda, na mesma máquina, com minutos de intervalo.** Para
uma tabela que vai para um TCC, o que importa é o pior caso, e no pior caso o
mínimo está cinco vezes à frente.

**A ressalva honesta, porque ela existe.** Com *muitas* repetições os dois
convergem: a mediana das 21 reproduziu entre rodadas dentro de 1,0 % (1 thread) e
0,5 % (8 threads), tão bem quanto o mínimo. A vantagem do mínimo **não é o limite,
é a velocidade de convergência** — mín-de-7 já vale 1,6 %, enquanto
mediana-de-7 vale 7,5 %. Isso importa porque a tabela de T4 tem dezenas de
células e o orçamento de repetição por célula é pequeno.

**A média está desqualificada** e é a única das três com argumento a priori
contra: numa distribuição unilateral ela soma a contaminação ao sinal por
construção. Mediu-se 10,4 % a 1 thread, o pior dos três naquela rodada.

### D3 — onde a variância mora, e ela tem nome

O `total` da sonda é a **soma dos sete slots** (`spread.c:111`), não um cronômetro
independente — não há resíduo não marcado onde a variância possa se esconder.
Logo, se o total espalha mais que a fase 4, algum slot espalha mais ainda, e ele
está na tabela. Coeficiente de variação por etapa, rodada 2:

| etapa | cv a 1 thread | cv a 8 threads |
|---|---:|---:|
| `pool` | — (não há thread) | **38,7 %** (e 42,6 % na rodada 1) |
| `zera delta` | **24,7 %** (17,9 % na rodada 1) | 7,9 % |
| `fase 5` | 11,9 % | 4,2 % |
| `filtros` | 8,5 % | 2,3 % |
| `merge` | 6,6 % | 2,2 % |
| **`fase 4`** | **3,3 %** | **2,9 %** |
| total do ano | 7,4 % | 5,5 % |

Três leituras, e a terceira é a que muda uma decisão:

1. **A fase 4 é a etapa mais estável do programa**, nas duas contagens de thread e
   nas duas rodadas (2,6 % a 8,6 % em quatro células). É por isso que §B26
   reproduziu dentro de 2 % entre sessões enquanto o ano não reproduziu. §B29
   estava certo e agora se sabe *por quê*: não é a granularidade em abstrato, é
   que as etapas ruidosas são outras.
2. **`zera delta` é a etapa ruidosa do lado serial** — mínimo 18,80 ms, máximo
   35,71 ms, quase o dobro. É escrita pura sobre a grade inteira, a varredura
   mais *memory-bound* das quatro (§B22 mediu o pior ganho, 2,06×), e portanto a
   mais exposta a quem mais disputa banda. Candidata a investigação em T4, e há
   uma hipótese barata de testar: se o custo se concentrar no **primeiro ano**, é
   falta de página no primeiro toque de `g_delta`, e `--repeat` a elimina de
   graça.
3. **`pool` é o maior injetor de ruído do lado paralelo**, disparado: cv de 38,7 %
   e 42,6 %, com mínimo 5,46 ms e máximo 13,98 ms para o mesmo
   `pthread_create` de sete threads. **Isto reclassifica o pool persistente.**
   Ele estava ranqueado em terceiro por *tamanho* (6 % do ano, §D8 do S06); por
   *contribuição ao ruído* é o primeiro. Continua fora do escopo (§S) — mas o
   argumento a favor dele deixa de ser só velocidade e passa a ser também
   qualidade de medida, e isso precisa estar escrito antes que alguém o descarte
   de novo por "só 6 %".

   **E é por isso que a tabela de T4 sai por etapa e não só por total**: publicar
   por etapa remove o ruído do `pool` da comparação sem precisar consertar o pool.

### D4 — VS12 está escrita com o estimador errado, e VS13 não precisa mais do T7

As duas verificações adiadas desde o S03 foram relidas contra §D2, e as duas
mudam de enunciado. Este é o achado mais concreto da sondagem.

**VS12.** O texto em `specs/S03-harness.md:336-339` diz:

> **VS12** — **V11:** a região cronometrada exclui I/O. Verificável: `--dump` e
> `--hash` não alteram o tempo relatado além do ruído, e **`--repeat 5` reporta
> mediana**. ADIADA para o S08, com T8.

A primeira metade continua boa. A segunda especifica **mediana**, e §D2 acabou de
medir que a mediana é o estimador com o pior "pior caso" dos três (23,8 %). A
verificação foi escrita em 17/09, quando ninguém tinha medido a forma da
distribuição — não é descuido, é a mesma razão pela qual os specs se escrevem um
de cada vez. Mas implementá-la como está consagraria a escolha errada **dentro da
suíte**, que é onde ela ficaria difícil de rever.

VS12 passa a cobrar: `--repeat N` reporta **os três** estimadores, e o que o
projeto publica é o **mínimo**. Ver T1 e T2.

**VS13.** Cobra que "a fração urbana final do `testgen` fique dentro de uma faixa
estreita entre 200×200 e 2000×2000 (hoje varia de 7,8 % a 0,67 %)", e está adiada
esperando T7, um gerador que escale. **§B25 do mestre já resolveu o problema por
outro caminho:** `harness/mktiled.sh` ladrilha o `demo200` e a fração urbana sai
**exatamente** igual — 9,70 % nas duas grades —, com V3 valendo na grade
resultante.

A dívida que VS13 protege é "não medir desempenho em grade vazia" (§B20/§B24), e
o caminho ladrilhado a quita com folga. O que o `testgen` ainda daria é uma
cidade **diferente** da do `demo200`, que é variedade de modelo — legítimo, e não
é do que a seção 4.2 precisa. Então VS13 é repontada para o caminho ladrilhado
(T3) e **T7 sai da lista de dívidas do S08** para trabalho futuro declarado.

### D5 — o que já está medido, e que este estágio NÃO deve remedir

Para que o estágio não gaste seu orçamento reconfirmando o que já passou no teste
de remedição de §B25:

| já medido e remedido | onde | não refazer |
|---|---|---|
| a trava global é o que impedia a fase 4 de escalar (+130 %/+128 %) | §B26, §B2 do S07 | a tabela A/B/C/none da fase 4 |
| B e C empatam; a escolha é de modo de falha | §B26 | a comparação B vs C |
| a curva de granularidade de B (1/8/64/1024/16384) | §B6 do S06, §B2 do S07 | é material gráfico pronto |
| o teto das varreduras é banda de memória, 2,24× e não 9,2× | §B22 | o eixo de threads das quatro varreduras |
| a entrada decide qual gargalo se enxerga (+130 % vs −1 %) | §D3 do S06 | a comparação ladrilhado vs artificial |

O que **falta** medir, e é o que T4 acrescenta: a tabela com o **eixo de grade**
(§T do mestre pede "grades grandes"), publicada por etapa, com o estimador de §D2
e o registro durável de T5. E a conta fechada de banda de memória que §B22 deixou
pedida.

### D6 — a máquina tem 6 núcleos físicos, e a escada de threads atravessa isso sem medir

Conferido com `lscpu` em 20/09, e é **§B30 do mestre**: AMD Ryzen 5 4600G, **6
núcleos físicos, 12 lógicos com SMT 2×**, um soquete, um nó NUMA, L3 de 4 MiB
compartilhado entre os seis, 7 GiB de RAM para o WSL. Os lógicos pareiam `0/1`,
`2/3`, … `10/11`.

O `SPEC.md` afirmava "12 núcleos" e que eles "cobrem **confortavelmente** a
matriz {1,2,4,8} threads de S08". **A 8 threads seis workers pegam núcleo
inteiro e dois dividem com um irmão SMT** — e como as etapas são separadas por
barreira, **o worker mais lento dita o ritmo de todos**, então dois workers
degradados degradam a fase inteira.

**Isto é um confundidor, não uma correção de §B26.** A comparação A vs B vs C
continua válida: o efeito atinge as três igualmente, e é por isso que B e C
encostam no piso enquanto A não encosta. O que fica contaminado é a afirmação de
**escalada** — "1,70× sob A contra 2,27×/2,29× sob B/C" mistura a trava saturando
com os núcleos físicos acabando, e a escada `{1,2,4,8}` **atravessa 6 sem nunca
medi-lo**.

→ **A escada passa a ser `1, 2, 4, 6, 8, 12`.** O ponto de 6 é o mais informativo
que falta no projeto inteiro e custa quase nada: é o último antes do SMT entrar,
e é o que separa "a trava saturou" de "acabaram os núcleos". O de 12 mostra o
teto do SMT. **Até T4 rodar, nenhuma atribuição de causa para o joelho entre 4 e
8 threads vai para o texto** (§B30).

### D7 — o desenho da matriz: duas fatias, não o produto cartesiano

**O limite não é a máquina.** Medido: 2000×2000 com 8 threads custa 1,04 s de
relógio e **192 MB de pico de RSS** — ≈48 bytes por célula. Extrapolando contra
os ~4 GiB disponíveis:

| grade | K do `mktiled.sh` | células | RSS estimado | veredito |
|---|---:|---:|---:|---|
| 2000×2000 | 10 | 4 M | 192 MB (medido) | folgado |
| 4000×4000 | 20 | 16 M | ~770 MB | **o topo útil** |
| 6000×6000 | 30 | 36 M | ~1,7 GB | teto prático |
| 8000×8000 | 40 | 64 M | ~3,1 GB | **não** — risco de swap, e swap destrói a medida |

O cruzamento completo — 5 grades × 6 threads × 4 variantes × 7 repetições = 840
execuções — custaria ~15 min. **Cabe, e ainda assim é o desenho errado.** O
motivo não é custo, é legibilidade: as células extras reafirmam §D3 do S06, que
já caracteriza a interação grade×variante pela fração urbana. Duas fatias
legíveis valem mais num TCC que um cubo que ninguém lê.

| fatia | eixos | fixo | execuções | responde |
|---|---|---|---:|---|
| **1 — escalabilidade** | grade × threads | variante **B** | 5 × 6 × 7 = 210 | o resultado principal: onde o ganho satura e por quê (§B22) |
| **2 — região crítica** | variante × threads | grade **2000×2000** | 4 × 6 × 7 = 168 | §B26 estendido com o ponto de 6, que é o que resolve §D6 |

Grades da fatia 1, todas por `mktiled.sh` com `K ∈ {1, 2, 5, 10, 20}` → 200, 400,
1000, 2000, 4000. **A virtude é que `K` é o único botão:** a fração urbana fica
travada em 9,70 % nas cinco, então o eixo varia **tamanho e só tamanho** — que é
o que torna a curva interpretável, e é o que a grade artificial não dá (§B20).

**E sobre o esqueleto do orientador:** a seção 4 pede "arquivos grandes
(100 x 100?)". 2000×2000 tem **400× mais células** que isso. O projeto já excede
o pedido com folga — a matriz não precisa **crescer** para satisfazer o TCC,
precisa ter a **forma** certa para mostrar os dois fenômenos. Ver §8 do HANDOFF,
que já registrava esse descasamento.

### D8 — não existe ambiente fechado para este benchmark, e é melhor saber antes

O teto medido é **banda de memória** (§B22), e banda **não se particiona**:
`cpuset`, container, VM e `taskset` repartem **núcleos**. Com um único
controlador de memória, um nó NUMA e 4 MiB de L3 compartilhado (§D6), nenhum
mecanismo de isolamento disponível nesta máquina fecha o ambiente para o recurso
que de fato limita a medida.

**Então o objetivo é reproduzível e declarado, não fechado** — que é o que §V15 e
§V12 já exigem e o que o `baseline.md` de T5 existe para registrar. O protocolo,
em ordem de retorno:

1. **Derrubar a pilha Docker** (`docker stop $(docker ps -q)`, reversível). São
   10 containers no ar — Kafka, RabbitMQ, três Redis, Postgres, memcached e duas
   APIs Java. ⚠ **É o ambiente de trabalho do autor: não se derruba sem pedir.**
2. **Fixar nos núcleos físicos:** `taskset -c 0,2,4,6,8,10`. É o **único**
   isolamento que ajuda aqui, e ajuda por remover uma variável (SMT), não por
   fechar o ambiente.
3. **Fechar o lado Windows** — o WSL2 é uma VM no mesmo hardware.
4. Rodar, tomar o **mínimo** (§D2), registrar tudo em `baseline.md`.

⚠ **A armadilha de verificação:** `load average` **não** detecta esta
contaminação. Medido em 20/09 com os 10 containers de pé: `0,55` em 12 lógicos,
que qualquer checagem superficial leria como máquina ociosa. A disputa é por
banda, não por CPU — Kafka, Redis e as JVMs consomem pouca CPU e muita banda, em
rajadas. **Conferir `load average` antes de medir daria luz verde e estaria
errado**, e é por isso que o piso de §D1 é a régua, não a carga reportada.

---

## §T tarefas

Status: `.` pendente · `>` em andamento · `x` concluído · `~` concluído com desvio

| id | st | tarefa | cita |
|----|----|--------|------|
| T0 | x | **Remedir §D1/§D2 em outra sessão** (§B25). `./harness/probe-s08-ruido.sh` versionada e **não modificada**. Roda **sozinha**. O achado deste spec é metodológico e serve para escolher o estimador de todo o resto — se ele não reproduzir, T1–T5 estão construindo sobre areia, e a discrepância é o achado do estágio. **Primeira, e bloqueia T4** | V11 |
| T1 | . | **`--repeat N`** (a T8 do plano). Roda a simulação N vezes no mesmo processo e reporta **mínimo, mediana e média** das N — as três, não uma. Publicar as três é o que torna a régua do ruído visível em toda saída, em vez de depender de quem lembrar de calculá-la (§B29). A região cronometrada continua excluindo I/O (V11) | V11 |
| T2 | . | **VS12**, com o enunciado corrigido por §D4: `--dump`/`--hash` não alteram o tempo relatado além do ruído, **e** `--repeat` reporta os três estimadores. A correção do enunciado de S03 é registrada, não silenciosa (§V12) | V11 |
| T3 | . | **VS13**, repontada por §D4 para o caminho ladrilhado: a fração urbana se preserva entre `demo200` e `demo2000` (9,70 % nas duas). Fecha a adiada **sem escrever o `testgen`** | V15 |
| T4 | . | **A tabela, em DUAS FATIAS e não no produto cartesiano (§D7).** Fatia 1, escalabilidade: grades `K ∈ {1,2,5,10,20}` × threads **{1,2,4,6,8,12}**, variante B fixa. Fatia 2, região crítica: {A,B,C,none} × as mesmas seis contagens, grade 2000×2000. As duas **por etapa**, com o estimador de T0/§D2 e a linha de 1 thread lida primeiro como régua (§B29). **O ponto de 6 threads é obrigatório** — é o que separa a trava saturando dos núcleos físicos acabando (§D6/§B30). Inclui a conta de banda que §B22 pediu e a hipótese de primeiro toque de `zera delta` (§D3.2) | V11, V15 |
| T4b | ~ | **Quantificar a carga de fundo**, que é a metade empírica da questão 2 do §10 do HANDOFF: a mesma sonda com a pilha Docker de pé e derrubada, comparando **piso contra piso** (§D8). Roda **depois** da T0, nunca antes — parar os containers antes da remedição transformaria "outra sessão" em "outra máquina", que é exatamente o erro que §B25 existe para evitar. ⚠ **Derrubar os containers exige pedir ao autor**: é o ambiente de trabalho dele | V11, V15 |
| T5 | . | **`harness/baseline.md`** (a T11 do plano): o registro durável do baseline cronometrado, para que o texto cite número escrito e não memória de sessão | V11, V15 |

**Onde o estágio parou (sessão 12).** T0 fechou e **§D2 reproduziu**, então o
aviso abaixo não disparou e T1–T5 seguem como escritas. T4b fechou **com
desvio**: precisou de três tentativas, a primeira mediu com os containers no ar
sem que ninguém percebesse (§B4), e a resposta que ela deu não é a que §D8
esperava (§B3). O desvio virou guarda — toda sonda desta família agora aborta se
achar container de pé.

**E a T4 ganhou um pré-requisito que não estava listado:** `probe-s06.sh` tem
`for n in 1 2 4 8` cravado nas linhas 124 e 213 e não aceita outra escada. Sem um
botão `THREADS`, a fatia 2 não consegue medir o ponto de 6 que §D6 exige. Ver
§B6.

**T0 primeiro e sozinha**, pela mesma razão que valeu no S07: é a única sensível a
carga de fundo, e a máquina tem dez containers no ar (§D8). T1→T2 são
sequenciais. T3 é independente e pode ir a qualquer momento. T4 depende de T0 e
T1; **T4b depende da T0 ter rodado com a máquina como está**. T5 é o último por
construção, e é onde o ambiente de §D8 fica registrado.

**O aviso do estágio.** Se T0 não reproduzir §D2 — isto é, se numa terceira
sessão o mínimo também se mostrar instável —, então **nenhum estimador serve
nesta máquina** e o S08 muda de forma outra vez: a tabela sai só por etapa, com
as etapas estáveis (fase 4, filtros, merge) e sem o total do ano, e a limitação
vai declarada para o texto. Isso é V15 fazendo o trabalho dela, não uma falha do
estágio. Não se conserta em silêncio (§V12), e a questão 2 do §10 do HANDOFF
— qual máquina para as medições — passa a ser bloqueante em vez de aberta.

---

## §V verificação do estágio

O estágio fecha quando:

1. **`./tests/run_checks.sh` passa com ZERO adiadas.** A linha final deixa de
   dizer `com 2 verificacao(oes) adiada(s) para o S08` — é o sinal visível de que
   a dívida do S03 fechou, e ela está aberta desde 17/09.
2. **T0 reporta se §D1/§D2 reproduzem**, com a tabela de estimadores ao lado da
   de 20/09. Reproduzindo ou não, vira §B do estágio; não reproduzindo, sobe para
   §B do mestre antes de qualquer número ir para o texto.
3. **`--repeat N` tem dentes:** com `N=1` a saída é a de hoje; com `N>1` os três
   estimadores aparecem e o mínimo é ≤ mediana ≤ máximo por construção — uma
   asserção, não uma esperança. E `--repeat` **não** altera `z`: a simulação é a
   mesma, então VS21 continua valendo byte a byte.
4. **A tabela de T4 é publicada por etapa**, com a linha de 1 thread presente em
   cada bloco como régua do ruído daquela execução. Nenhuma comparação de total
   do ano entre variantes (§B29).
5. **Nenhuma regressão:** `make v7`, `make tsan`, o valgrind (serial e VS30) e o
   `z` do `demo200` contra o golden pré-S05 continuam limpos.
6. **`harness/baseline.md` existe e é citável**, com data, máquina, carga de fundo
   declarada e o estimador usado escrito ao lado de cada número.

---

## §B achados do estágio

Sessão 12, 20/09 às 23h. Quatro rodadas da **mesma sonda não modificada**
(`harness/probe-s08-ruido.sh`, `REP=21 GRUPO=7`), saídas cruas em
`../sleuth-par/harness/out/`. Cada uma traz o SHA, a confirmação de sonda limpa
e o censo do ambiente antes e depois.

### B1 — §D2 reproduz e fica mais forte; §D1 reproduz pela metade

**O aviso do §T não disparou: o estágio segue como está escrito.** O mínimo
continua sendo o estimador, e por margem maior do que a sondagem media.

Mas §D1 tem duas afirmações e só uma sobreviveu. O **mecanismo** — contaminação
unilateral, não deriva — reproduziu de novo: a coluna "1ª/2ª metade" deu −1,2 %
a 1 thread e +1,9 % a 8, sinais opostos, e o `pool` deu +16,4 % depois de
+90,5 % e −24,3 % nas rodadas anteriores. Deriva não troca de sinal três vezes.

A afirmação de que **o piso é o que reproduz entre sessões, dentro de ~2 %, não
se sustentou**:

| mínimo do ano | sessão 11 (r2) | T0 (sessão 12) | distância |
|---|---:|---:|---:|
| 1 thread | 164,37 | 174,50 | **+6,2 %** |
| 8 threads | 88,53 | 94,63 | **+6,9 %** |

É §B25 do mestre acontecendo ao vivo: **o mínimo protege contra pico dentro da
série, não contra uma sessão inteira mais carregada.** A consequência prática
para a T4 é que a tabela tem de ser **internamente consistente** — todas as
células da mesma execução —, porque nem o piso transfere entre sessões.

### B2 — a etapa reproduz onde o total não, e agora o contraste está no mesmo dado

Fase 4 sob A, grade ladrilhada, **quatro sessões**, com a mediana (o estimador
que a `probe-s06.sh` usa e que produziu §B26):

| | s09 (§B26) | s10 (§B29) | s12 T0 | s12 limpa |
|---|---:|---:|---:|---:|
| 1 thread | 83,64 | — | 83,87 | **82,79** |
| 8 threads | 35,48 | 35,40 | 37,46 | **35,18** |

Com a máquina limpa, **dentro de 1 % nas duas pontas, quatro sessões depois**.
E na mesma execução da T0 em que a etapa ficou a 1 %, o total do ano andou 7 %.

"Compare etapas, nunca totais" (§B29) deixa de ser regra de prudência e vira
resultado medido, com o controle ao lado em vez de em outro documento.

Um detalhe que fecha o argumento de §D2: os 35,48 de §B26 são **mediana de uma
sessão quieta**. Na T0, carregada, a mediana inflou para 37,46 e **o mínimo deu
35,53** — o mínimo de uma sessão suja reproduz a mediana de uma limpa, que é
exatamente o que §D2 prevê ao dizer que o mínimo estima o custo do código e a
mediana estima código mais carga típica.

### B3 — T4b: a pilha Docker não move o piso; move a *estabilidade*

Piso contra piso, três ambientes na mesma sessão:

| | tudo carregado | só Windows limpo | tudo limpo |
|---|---:|---:|---:|
| piso 1 thread | 174,50 | 166,88 | 170,91 |
| piso 8 threads | 94,63 | 88,88 | 90,51 |
| razão 1/8 | 1,844 | 1,877 | 1,888 |
| espalhamento do ano, 1t | 18,3 % | 41,7 % | **8,4 %** |
| cv da fase 4, 1t | 1,9 % | 5,9 % | **0,8 %** |

**Derrubar os dez containers não melhorou o piso — piorou 2,4 %.** Os três
ambientes ficam dentro de ~4 % e o sinal não é consistente. O que a limpeza
comprou foi **variância**: espalhamento de 41,7 % para 8,4 %, cv da fase 4 de
5,9 % para 0,8 %.

→ **Resposta empírica à questão 2 do §10 do HANDOFF.** O ambiente vale ≤ 4 % no
piso, com sinal inconsistente. É limitação declarável, não impedimento, e
**não se troca de máquina por causa disso**. O que se declara é o protocolo:
medir com a máquina limpa não por causa do piso, e sim porque só assim o
espalhamento cabe na precisão que a tabela publica.

→ E **corrige uma expectativa que este spec carregava sem medir**: §D8 ordena o
protocolo "em ordem de retorno" com derrubar o Docker em primeiro lugar. Pelo
piso, ele é o de menor retorno dos três; pela variância, é o maior. A ordem
estava certa pelo motivo errado.

### B4 — derrubar o Docker pelo Windows não derruba o Docker, e o canal de confirmação concorda com quem errou

Parar o `com.docker.service` no Windows **reinicia o WSL**; ao subir, o systemd
religa o `docker.service`, que está `enabled`, e os dez containers voltam
**recém-nascidos** — o estado mais caro deles, com JVM aquecendo e cache frio.

A lista de serviços do Windows reportou `Stopped` em tudo, e estava **certa**:
os containers nunca estiveram lá. A primeira tentativa da T4b mediu assim, e o
dado denuncia sozinho — a série de 1 thread **começa em 239,00 ms e decai** até
~167, com "1ª/2ª metade" em −9,9 %. Não é o piso-com-picos de §D1: é decaimento
de boot, que o projeto nunca tinha visto porque nunca mediu logo após um
restart.

Custou uma rodada, e só não custou mais porque **o censo estava no mesmo arquivo
da medida**. Virou a armadilha 8 do HANDOFF e uma guarda no roteiro: a segunda
tentativa **aborta sem medir** se houver container de pé.

É a família das armadilhas 6 e 7 — o canal de confirmação quebrado, não o
trabalho — e a saída é a mesma das outras duas: escrever num arquivo e ler o
arquivo.

### B5 — a série multithread é bimodal, e a hipótese que eu previ foi refutada

Com a máquina limpa, o total do ano não é ruído em torno de um centro: são
**dois aglomerados com vão vazio entre eles**, e vão vazio significa causa
discreta.

| threads | modo baixo | modo alto | vão | separação |
|---:|---|---|---|---:|
| 1 | — | — | contínuo | **unimodal** |
| 6 | 84,5–88,0 (11×) | 90,0–96,2 (10×) | 2,0 ms | +6,4 % |
| 8 | 90,1–94,0 (13×) | 98,0–102,8 (7×) | 4,0 ms | +8,9 % |
| 12 | 96,5–102,2 (11×) | 105,7–110,3 (10×) | 3,6 ms | +8,6 % |

**Reproduz:** 8 threads deu a mesma estrutura em duas execuções independentes,
13 baixo / 8 alto nas duas, separação 8,6 % e 8,9 %. E é **exclusivo do
multithread** — a 1 thread a série é contínua.

**A previsão registrada antes de medir caiu.** Ela dizia: a 8 threads em 6
físicos dois núcleos dobram e *quais dois* varia, então há liberdade de
posicionamento e daí a bimodalidade; a 12 threads em 12 lógicos não há escolha
a fazer, então seria unimodal. **12 é bimodal, com a mesma magnitude.** A
liberdade de posicionamento não é o mecanismo.

**A causa fica em aberto, de propósito.** A separação ser relativa e quase
constante (6–9 %) em três contagens sugere efeito multiplicativo — relógio,
estado de energia — e não "um worker lento". Mas foi assim que §B23 caiu:
atribuindo a cache o que era carga de fundo. Fica fenômeno medido, causa por
nomear, e a sonda que a nomearia ainda não existe.

⚠ **Consequência para a T4, que não estava no spec:** a 8 threads o mínimo
deixa de significar "a execução menos contaminada" e passa a significar **"o
melhor dos dois modos"**. Continua sendo o estimador certo — é o único que
escolhe um modo e fica nele —, mas o que ele estima mudou de nome, e a tabela
tem de dizer isso.

### B6 — o joelho está em 6 threads, e ainda não é atribuição de causa

Máquina limpa, variante A, mínimo, medições adjacentes no mesmo boot. As duas
execuções se sobrepõem em 8 threads e concordam em **0,5 %** (90,51 e 90,07) —
é isso que autoriza emendá-las, e é o teste que §B27 pede antes de qualquer
emenda:

| threads | ano | escalada | fase 4 | escalada |
|---:|---:|---:|---:|---:|
| 1 | 170,91 | 1,00× | 82,44 | 1,00× |
| **6** | **84,54** | **2,02×** | **27,54** | **2,99×** |
| 8 | 90,07 | 1,90× | 33,47 | 2,46× |
| 12 | 96,51 | 1,77× | 37,50 | 2,20× |

**O pico é em 6 threads — a contagem de núcleos físicos — e piora
monotonicamente depois.** §B26 tinha visto a regressão de 4 para 8 sob A; agora
o vértice tem lugar, e §B30 tem a medida que o motivou.

⚠ **Mas isto não separa as duas causas**, que era o serviço que §D6 pedia ao
ponto de 6. A sonda fixa `--variant A`, então o que está na tabela é a curva da
trava **somada** à dos núcleos. Há indício de que a trava domina — §B26 mostra
`none` melhorando de 4 para 8 threads, o que núcleos esgotados não permitiriam
—, **mas esse indício emenda duas sessões, duas sondas e dois estimadores**, que
é precisamente o splice que §B27 proíbe.

→ **O que resolve é a fatia 2 da T4**, numa execução só. E ela precisa de uma
coisa que ainda não existe: `probe-s06.sh` tem `for n in 1 2 4 8` cravado nas
linhas 124 e 213, sem botão `THREADS`. A T4 exige `{1,2,4,6,8,12}` de qualquer
forma, então o botão é pré-requisito dela, não trabalho novo.

→ **Até lá, §D6 continua valendo palavra por palavra:** nenhuma atribuição de
causa para o joelho vai para o texto.

### B7 — o mínimo ganhou cinco vezes, e a quinta por um motivo diferente

Espalhamento entre grupos de 7, todas as células medidas nesta sessão:

| ambiente | threads | mín. | mediana | média |
|---|---:|---:|---:|---:|
| carregado | 1 | **2,1 %** | 7,9 % | 7,2 % |
| carregado | 8 | **3,8 %** | 11,9 % | 3,5 % |
| containers frescos | 1 | **0,1 %** | 20,3 % | 16,1 % |
| containers frescos | 8 | **2,8 %** | 13,0 % | 7,6 % |
| limpo | 1 | **0,4 %** | 1,3 % | 2,0 % |
| limpo | 8 | **1,5 %** | 2,1 % | 1,8 % |
| limpo | 6 | **2,9 %** | 5,6 % | 3,7 % |
| limpo | 12 | **1,6 %** | 7,5 % | 2,2 % |

O mínimo ficou **≤ 3,8 % nas oito células**; a mediana chegou a 20,3 %. Numa
série que decaía 30 % por causa do boot dos containers, a mediana espalhou
20,3 % e **o mínimo espalhou 0,1 %**.

E o argumento de §D2 ganha um segundo andar que ele não tinha: nas quatro
células de máquina **limpa**, onde a história de contaminação unilateral não se
aplica, o mínimo continua na frente — porque ele também é o estimador que
sobrevive à **bimodalidade** de §B5. Ele escolhe um modo e fica nele; a mediana
oscila entre os dois conforme a proporção do sorteio.

**Duas razões independentes para o mesmo estimador.** §D2 tinha uma.

---

## §N notas para o texto

- **A tríade metodológica vira quarteto, e o quarto está medido.** §D3 do S06: a
  **entrada** decide qual gargalo o experimento enxerga. §B27: a **ordem** das
  execuções decide o sinal. §B29: a **granularidade** da medida decide se há
  sinal. E agora §D2 daqui: o **estimador** decide se o resultado reproduz — a
  mesma sonda, duas rodadas com minutos de intervalo, deu respostas opostas sobre
  qual estimador serve. Os quatro são parâmetros do experimento que **não estão
  no código medido**, e é por isso que são material de seção 4 e não de seção 3.
- **A forma da distribuição é o argumento, não o desvio padrão.** A série impressa
  em §D1 é um piso com excursões para cima, e ler isso exige olhar a série crua —
  nenhuma estatística de duas metades distingue "derivou" de "teve picos no fim".
  É um bom exemplo, curto e verdadeiro, de por que resumir cedo demais destrói a
  informação de que se precisa. O gráfico da série crua vale mais que a tabela de
  cv.
- **O caso de VS12 é o de `par.c:118` do outro lado.** §B1 do S07 achou um
  comentário que afirmava cobertura inexistente; aqui a verificação existe, está
  escrita, e especifica o **critério** errado — e ficou cinco estágios esperando
  para ser implementada exatamente como estava. O padrão é o mesmo da armadilha 4
  do HANDOFF ("desconfie do critério antes do objeto"), pela **sexta** vez, e numa
  cara nova: critério errado dentro de uma verificação ainda não escrita, que é o
  lugar mais barato possível para encontrá-lo. Se T8 tivesse sido feita no S03
  como planejada, a mediana estaria na suíte hoje.
- **`zera delta` merece uma frase na seção 4.2**, e ela liga duas medidas que o
  projeto já tem: é a varredura com o pior ganho paralelo (2,06×, §B22) *e* a
  etapa serial mais ruidosa (cv 24,7 %, §D3). As duas coisas têm a mesma causa —
  é escrita pura sobre a grade inteira, logo é pura banda de memória, logo ganha
  pouco com threads e sofre muito com quem disputa a banda. **Um mesmo mecanismo
  explicando um número de desempenho e um número de variabilidade** é o tipo de
  coerência que vale mostrar.
- **A questão aberta que virou premissa é o melhor caso do projeto no padrão da
  armadilha 4** (§B30), e é melhor até que `par.c:118` do S07. Lá havia uma
  afirmação não verificada; aqui o projeto **escreveu, na sessão 02, que a
  verificação faltava** — "a de desenvolvimento tem 12 núcleos; falta saber se é
  a definitiva e **se SMT está ligado**" — e dois documentos depois afirmou que
  esses núcleos "cobrem confortavelmente" a matriz do S08. Ninguém decidiu nada:
  a pergunta simplesmente parou de ser feita. **Para a seção 4.1, é o exemplo
  mais econômico de que registrar uma dúvida não a resolve** — e de que o
  documento que a registrou (`history/`) é justamente o que ninguém relê.
- **E o corolário técnico é bom material de 4.2:** a conclusão que o SMT
  contamina não é a comparação entre variantes, é a **escalada**. Vale mostrar a
  distinção, porque ela é o tipo de coisa que separa medir de entender: o efeito
  atinge A, B e C igualmente, então a diferença entre elas sobrevive intacta,
  enquanto o número "escalou 2,27×" não sobrevive. **Comparação relativa é
  robusta ao confundidor comum; afirmação absoluta não é.**
- **O pool persistente ficou de fora três vezes, e pelo motivo errado nas três.**
  Foi rebaixado de primeiro para terceiro candidato por tamanho (6 % do ano) no
  S06, e declarado fora do S07 e do S08 por risco e prazo. §D3.3 mostra que por
  contribuição ao **ruído** ele é o primeiro, com cv de ~40 %. Como trabalho
  futuro declarado, o argumento correto a escrever não é "sobrou 6 % de ganho", é
  "o `pthread_create` por ano é o que mais atrapalha medir o resto".
