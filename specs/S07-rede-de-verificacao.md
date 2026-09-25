# S07 — a rede de verificação: o que o projeto afirmava sem medir

**Estágio:** S07 · **Status:** concluído (20/09) · **Dia no plano:** 22/09 (escrito e executado em 20/09)
**Mestre:** `../SPEC.md` · **Cita:** V3, V4, V6, V7
**Alimenta o texto:** §4.1 (corretude e determinismo) e §2.2 (o que uma suíte prova)

**O recorte deste estágio foi arbitrado por auditoria, e a auditoria esvaziou a
linha do mestre.** O §T diz "S07 — corretude e determinismo: 1 vs 2 vs 4
threads". Isso está feito e automatizado desde o S05 e o S06: VS19 (V3 em
1/2/4/8), VS20 (estatísticas), VS22 (V7 sob `--threads`), VS24/VS25 (as três
variantes), VS27 (as quatro com uma thread). Escrever o S07 como a linha o
descreve produziria verificação que já existe.

O que sobra é outra coisa, e os cinco itens têm a mesma forma: **propriedades
que o projeto afirma por argumento, em documento durável, e nunca mediu** — e
desta vez o padrão está dentro da própria rede de verificação. Ver §D1.

**E a sondagem que precedeu este spec mudou o que o estágio é.** Os cinco
buracos foram sondados antes de catalogados (§R1 do mestre, e a regra de
`README.md`), e **nenhum deles é um defeito**: as cinco propriedades são
verdadeiras. O estágio deixa de ser "caçar o que quebrou" e passa a ser
"transformar argumento em medida, antes que o texto cite o argumento". Ver §D2,
que é a entrada mais importante deste spec.

---

## §G objetivo do estágio

Fechar a distância entre o que os documentos duráveis do projeto **afirmam** e o
que a suíte **sustenta**, convertendo cinco argumentos em cinco verificações
automatizadas — e corrigir a única afirmação de cobertura que é literalmente
falsa (`par.c:118`).

E, antes de tudo isso, cumprir a dívida que §B25 do mestre cobra de si mesmo:
**remedir as colunas B e C de §B26 em outra sessão**, porque elas saíram de uma
sessão só e a regra que o projeto aprendeu tarde é que achado de desempenho se
remede antes de virar afirmação. É a única tarefa aqui que **destrava o texto**,
e por isso é a primeira.

Este é o estágio menos vistoso do projeto e é o que dá direito de escrever a
seção 4. Uma suíte que verifica o que é fácil verificar, e argumenta sobre o
resto, é exatamente o objeto que §B25 manda desconfiar.

## §S escopo

**Entra:** a remedição de §B26 (T0); VS28–VS32; a correção do comentário de
`par.c:118`.

**Não entra**, e as exclusões são decisões, não omissões:

- **Qualquer medição de desempenho nova.** T0 é remedição de número existente,
  com a sonda versionada que já o produziu, sem alterar a sonda. Desempenho é o
  S08.
- **O pool persistente e a montagem da lista da fase 5.** Continuam declarados
  fora com o número que os justifica (§D8 do S06). Os dois mexem em ciclo de
  vida de thread ou em ordem de lista, e este estágio existe para **firmar** a
  rede, não para mexer no que ela cobre.
- **O `testgen` de T7** (§B20/§B25 do mestre). Continua devido, e continua sendo
  dívida do S08.
- **O texto.** O autor está escrevendo em paralelo. Não se encosta em
  `Esqueleto TCC.txt` nem em arquivo de texto do TCC.

**Não muda nenhuma regra do modelo, e quase não muda código.** A única edição em
fonte de produção prevista é um comentário. Se este estágio precisar alterar
comportamento para passar, o achado é grande e o spec está errado — ver o aviso
ao fim de §T.

---

## §D entradas conhecidas

Levantamento de 20/09, conferido no código real antes de este spec ser escrito.
A auditoria original está em §11 do `HANDOFF.md` e em §T do mestre; aqui ela foi
**reconferida linha a linha**, porque o próprio assunto do estágio é não confiar
em afirmação não medida — inclusive na afirmação de que os buracos existem.

### D1 — os cinco buracos existem, e a evidência confere

| # | o buraco | conferido em | o que se vê |
|---|---|---|---|
| 1 | V3 sob threads só foi verificada com **uma semente** | `tests/run_checks.sh:416` | `REF="--load $DATA --years 20 --seed 1 ..."`. VS19, VS20, VS21, VS24, VS25 e VS27 passam todas por `$REF`. As outras quatro sementes do projeto (2, 7, 42, 43) só aparecem nos testes do gerador |
| 2 | O `--scan reverse` **nunca rodou com threads** | `tests/run_checks.sh:374-379` | VS17 monta `V4="--load ... --seed 1 ..."` sem `--threads` e roda as quatro combinações de `--rng` × `--scan` em serial |
| 3 | O valgrind **nunca rodou com threads**, nem sob B ou C | `Makefile:66-68` e `run_checks.sh:51-63` | os dois alvos rodam `--rows 20 --cols 20 --years 5 --seed 1`, sem `--threads` e sem `--variant`. O vetor de `PAR_SHARDS` mutexes da variante B nunca foi alocado sob valgrind |
| 4 | `par.c:118` **documenta uma garantia inexistente** | `par.c:118` | "É o que acontece numa grade 20x20 com `--threads 32`, **que os testes exercitam**". Nenhum teste passa de 8: o maior `--threads` em `tests/` é 8, em VS19/VS20/VS24 |
| 5 | **§V3 do mestre afirma 16 threads; a suíte para em 8** | `SPEC.md` §V3, §B21; `run_checks.sh:433` | §V3 diz "verificado em 1, 2, 4, 8 e **16** threads" e §B21 repete. VS19 itera `for n in 2 4 8`, e o próprio `echo` da verificação diz "1/2/4/8". O 16 foi rodado à mão no S05 e nunca virou suíte |

O nº 5 é novo — não está na auditoria de §11 do HANDOFF — e saiu justamente de
conferir o nº 4 em vez de aceitá-lo. É o mesmo defeito do nº 4 num documento de
hierarquia mais alta: **o nº 4 mente num comentário, o nº 5 mente num
invariante.** Os dois se consertam com a mesma tarefa.

### D2 — os cinco passam, e é isso que decide o recorte

Sondados em 20/09 antes de este spec existir, com o binário de `make` corrente.
A sonda foi descartável de propósito: o que sobreviver a ela vira VS28–VS32,
versionado em `tests/run_checks.sh`, que é onde verificação mora.

| buraco | sondagem | resultado |
|---|---|---|
| 1 | sementes 2, 7, 42, 43 × {2,4,8} threads, `demo200`, 20 anos, `z` contra o serial da mesma semente | **12 de 12 idênticos byte a byte** |
| 2 | `--scan forward` contra `--scan reverse` em `anchored`, com 1, 2, 4 e 8 threads | **idêntico nas quatro**; e `legacy` diverge, então o teste continua com dentes |
| 3 | valgrind `--leak-check=full` com `--threads 8` nas variantes A, B e C | **0 erros, 0 vazamentos nas três.** "All heap blocks were freed" |
| 4 | 20×20 com 16, 32 e **64** threads (`nrows` 20 < N) | **rc=0 e `z` idêntico ao serial nas três.** A faixa vazia funciona |
| 5 | `demo200` com 16 e 32 threads | **idêntico ao serial** |

**As cinco propriedades são verdadeiras.** O estágio não vai consertar nada; vai
impedir que elas voltem a ser argumento.

Isto tem três consequências no recorte, e a terceira é a que importa:

1. **A ordem das tarefas fica livre.** Não há tarefa que possa cercar a causa de
   outra, porque não há falha a cercar. Então a ordem passa a ser por valor: T0
   primeiro, porque é a única que destrava o texto.
2. **O risco do estágio desabou.** O aviso de §11 do HANDOFF — "T1 e T2 podem
   falhar, e se falharem é o achado do estágio" — foi endereçado por medição, e
   a resposta é não. V3 não cai, e o instrumento de diagnóstico do projeto
   (`--scan reverse`) continua válido sob threads, que é o que §5 do HANDOFF
   afirmava sem medir.
3. **O achado do estágio muda de lugar.** Não está no que a suíte deixou passar;
   está em **quanto o projeto afirmava sem medir, e no fato de as duas coisas
   terem o mesmo cheiro e custos muito diferentes.** Cinco afirmações
   verdadeiras e não verificadas são um estágio de 40 minutos; uma falsa no meio
   delas teria sido descoberta pelo texto, depois de publicada. Ver §N.

### D3 — o valgrind com threads é barato, e a carga certa é o `demo200`

O buraco nº 3 é o único cuja verificação custa tempo de parede relevante, e a
suíte inteira roda em 9 s e é a porta de tudo. Então a carga de VS30 foi medida
antes de escolhida:

| carga | as três variantes, `--threads 8` | o que exercita |
|---|---|---|
| `--rows 20 --cols 20 --years 5` | 6,0 s | a alocação do vetor de mutexes, mas quase nenhuma tomada de trava — a grade é pequena e pouco urbana |
| `--load data/demo200 --years 20` | **7,7 s** (2,5 + 2,5 + 2,7) | a região crítica de verdade: fração urbana real de 9,70 %, 20 anos |

**A carga forte custa 28 % mais e vale muito mais**, então VS30 usa o `demo200`.
A diferença de custo é pequena porque o valgrind é dominado pela instrumentação,
não pelo tamanho da grade.

E **VS30 entra na suíte padrão**, não num alvo à parte. O argumento é o assunto
do estágio: um teste que não roda por padrão é um teste que não existe, e seria
incoerente fechar buracos de "afirmado e não medido" criando um alvo que ninguém
roda. O precedente está no lugar — VS2 já roda valgrind dentro da suíte. O custo
esperado é ~9 s → ~17 s, que continua barato para a porta de tudo.

### D4 — o que a suíte já cobre, para não se duplicar verificação

Conferido para que VS28–VS32 não repitam trabalho. Já existe e continua valendo:

- **VS19/VS20** — `z`, tabela anual e as invariantes de §B21 em 1/2/4/8, semente 1.
- **VS21** — `--threads 1` reproduz bit a bit a saída pré-S05, nos dois RNGs.
- **VS24/VS25** — o mesmo com o eixo de variante, A/B/C × {1,2,4,8}.
- **VS26** — TSan limpo em A, B e C, e acusando em `none`.
- **VS27** — as quatro variantes degeneram no mesmo serial com uma thread.

As cinco verificações novas são **ortogonais** a essas: mudam a semente (VS28),
a ordem de varredura (VS29), o instrumento (VS30), e a contagem de threads para
além de `nrows` (VS31) e para além dos núcleos (VS32). Nenhuma repete um eixo já
coberto.

---

## §T tarefas

Status: `.` pendente · `>` em andamento · `x` concluído · `~` concluído com desvio

| id | st | tarefa | cita |
|----|----|--------|------|
| T0 | x | **A remedição de §B26 (§B25).** `GRADES=ladrilhado REP=8 ./harness/probe-s06.sh`, sonda versionada e **não modificada**. As colunas B e C de §B26 saíram de uma sessão só. Roda **sozinha**, sem nada concorrente na máquina — §B25 caiu por carga de fundo, e repetir esse erro aqui seria o cúmulo. **Reproduz dentro de 2 % nas quatro colunas — §B2** | V11 |
| T1 | x | **VS28** — V3 com as sementes 2, 7, 42 e 43 sob {2,4,8} threads, `z` contra o serial da mesma semente | V3 |
| T2 | x | **VS29** — VS17 sob threads: `--scan reverse` contra `--scan forward` em `anchored`, com {2,4,8} threads; e a guarda de `legacy`, que tem de divergir | V4, V3 |
| T3 | x | **VS30** — valgrind com `--threads 8` nas variantes A, B e C sobre o `demo200` (§D3) | V6 |
| T4 | x | **VS31** + **corrigir `par.c:118`**: faixa vazia de verdade, 20×20 com 16, 32 e 64 threads, `z` idêntico ao serial. O comentário passa a dizer o que os testes exercitam **depois** que eles exercitam. Ver §B1 | V3 |
| T5 | x | **VS32** — `demo200` com 16 e 32 threads, que é o que §V3 e §B21 do mestre afirmam. Fecha o buraco nº 5 sem tocar em VS19 | V3 |

**As seis fecharam em 20/09, na ordem prevista.** A suíte roda **VS1–VS32** e
sai com `== tudo passou, com 2 verificacao(oes) adiada(s) para o S08 ==`.
Nenhuma verificação nova reprovou, o que confirma §D2 — e o aviso de §T continua
registrado para quem reler: se alguma delas reprovar no futuro, a primeira
suspeita é a sondagem, não o código.

**T0 primeiro, e sozinha.** É a única que destrava o texto e a única sensível a
carga de fundo. T1–T5 são independentes entre si e podem ir em qualquer ordem
depois dela.

**O aviso que continua valendo, apesar de §D2.** A sondagem rodou uma vez, num
binário só, numa máquina só. Se alguma das cinco verificações **falhar** quando
virar suíte, a discrepância contra §D2 é o achado — e a primeira suspeita é a
sondagem, não o código. Não se conserta em silêncio (§V12), e a cláusula de V3
continua disponível: "é rebaixada para equivalência estatística e a limitação é
declarada no texto, nunca silenciada".

---

## §V verificação do estágio

O estágio fecha quando:

1. **`./tests/run_checks.sh` passa VS1–VS32**, com VS12 e VS13 ainda adiadas
   para o S08 e nenhuma outra adiada. A linha final continua sendo
   `== tudo passou, com 2 verificacao(oes) adiada(s) para o S08 ==`.
2. **As cinco verificações novas têm dentes.** Cada uma tem de ser capaz de
   reprovar: VS29 exige que `legacy` **divirja** (senão a fase 4 não está sendo
   exercitada, e a verificação vira decoração — é a guarda que VS17 já tem);
   VS30 usa `--error-exitcode=1`; VS31 compara contra o serial, não contra si
   mesma.
3. **`par.c:118` descreve cobertura que existe**, e a descrição é conferível
   apontando para a verificação que a sustenta.
4. **T0 reporta a tabela de §B26 remedida**, com as colunas B e C ao lado das de
   19/09, e o spec registra se reproduz ou não. **Se não reproduzir, isso vira
   §B do estágio e sobe para §B do mestre** — e a tabela de §B26 é corrigida
   antes de o texto a citar, que é o motivo de T0 existir.
5. **Nenhuma regressão:** `make v7`, `make tsan` e o valgrind serial continuam
   limpos, e o `z` do `demo200` continua idêntico ao golden pré-S05.

---

## §B achados do estágio

### B1 — o comentário de `par.c:118` afirmava cobertura que nunca existiu

A linha dizia, sobre a faixa vazia do particionamento:

> É o que acontece numa grade 20x20 com `--threads 32`, **que os testes
> exercitam**.

O maior `--threads` em `tests/` era **8**. A segunda metade da frase era falsa
desde que foi escrita, no S05.

**O que estava certo e o que estava errado, porque a distinção é o achado.** O
mecanismo descrito é verdadeiro: com `nrows` 20 e 32 threads, 12 workers ficam
com `row_lo == row_hi`, os laços não executam e eles só participam das
barreiras — medido em §D2, com `z` idêntico ao serial em 16, 32 e 64 threads. O
que era falso é a **cobertura**: a frase transformava um raciocínio correto numa
garantia verificada, e quem lesse `par.c` antes de mexer em thread — que é o que
`par.h` manda fazer — sairia acreditando que havia uma rede que não havia.

**Por que este é o caso mais instrutivo do projeto.** É a quinta ocorrência do
padrão da armadilha 4 do HANDOFF, numa cara nova: não é critério de comparação
errado (§B14 do S03), nem medição isolada virando achado (§B25), nem "o código
calcula e joga fora" (§B8, §B15, §B16, §B17). É **cobertura afirmada e não
conferida**. E se distingue dos quatro primeiros num ponto que importa para o
texto: aqueles são defeitos do SLEUTH original, herdados; este foi escrito
**neste** trabalho, por quem já conhecia o padrão, no arquivo mais lido do
estágio anterior.

**A correção e a ordem dela.** O comentário agora cita VS31 e os três valores
que ela de fato exercita (16, 32, 64) — e foi corrigido **depois** de VS31
existir, não antes. A ordem não é cerimônia: inverter a ordem é exatamente o
erro original, que foi descrever a rede pretendida como se fosse a rede
existente. A frase antiga ficou registrada dentro do próprio comentário, com
ponteiro para este §B, porque §V12 do mestre proíbe correção silenciosa.

### B2 — §B26 reproduz, e a dívida que §B25 deixou em aberto está paga

T0, rodado em 20/09 às 16h51 em sessão diferente da que produziu §B26, com a
sonda **versionada e não modificada** (`GRADES=ladrilhado REP=8`, mediana de 8
contra a mediana de 5 do original). Fase 4 em ms, 8 threads, grade ladrilhada
2000×2000:

| origem | A | B | C | none (piso) |
|---|---:|---:|---:|---:|
| §B26, sessão 09 (19/09) | 35,48 | 15,48 | 15,25 | 15,40 |
| **T0, sessão 10 (20/09)** | **35,40** | **15,73** | **15,55** | **15,52** |
| diferença | −0,2 % | +1,6 % | +2,0 % | +0,8 % |
| custo sobre o piso, então | +130 % / +1 % / −1 % | | | |
| custo sobre o piso, agora | **+128 %** | **+1 %** | **+0 %** | — |

**As quatro colunas reproduzem dentro de 2 %**, num experimento cujo ruído
declarado é de 10 a 20 %. As três conclusões de §B26 ficam de pé:

1. **A trava global é o que impedia a fase 4 de escalar.** +128 % agora, +130 %
   antes.
2. **B e C empatam.** 15,73 contra 15,55 — 1,2 % de diferença, uma ordem de
   grandeza abaixo do ruído do experimento. O mecanismo mais sofisticado
   continua não sendo o mais rápido, e a escolha entre eles continua sendo de
   modo de falha, não de desempenho.
3. **Sob A, ir de 4 para 8 threads piora**: 31,70 → 35,40 na fase 4 e
   90,98 → 102,18 no ano. Sob B e C não piora (23,71 → 15,73 e 23,60 → 15,55).

E os dois números de apoio também reproduzem, o que importa porque eles são a
**guarda** da tabela:

| | sessão 09 | T0 |
|---|---|---|
| curva de granularidade de B (1 / 8 / 64 / 1024 / 16384 travas) | +146 % / +19 % / +6 % / +1 % / +3 % | **+150 % / +23 % / +4 % / +1 % / +2 %** |
| travas por célula: ladrilhado ÷ artificial | 325× | **326×** |

A linha de `PAR_SHARDS=1` continua custando o que A custa (+150 % contra
+128 %), que é a prova de que o mecanismo de B é o que o código diz — com uma
trava só, B **é** A.

**Isto encerra o arco de §B25.** §B26 é o segundo achado do projeto a passar no
teste de remedição, depois de §B22, e agora passou nas quatro colunas e não só
na de A. **As tabelas de §B26 podem ir para o texto.** Era a única coisa que o
código ainda devia à seção 4.

### B3 — a mesma execução mostra por que §B27 existe, e isso não foi de propósito

A tabela de T0 traz o ano inteiro ao lado da fase 4, e a linha de custo do ano
diz:

```
custo     +27%     -3%     -7%  (piso)
```

**B e C aparecem mais rápidas que o piso.** Isso é fisicamente impossível: o
piso é o mesmo binário **sem exclusão mútua nenhuma**, e nem B nem C podem
custar menos que não fazer nada. O ano de `none` a 8 threads deu 80,36 ms contra
77,57 de B e 74,75 de C; e a linha de 1 thread, em que as quatro variantes
degeneram no mesmo código serial (VS27), deveria dar quatro valores iguais e deu
180,08 / 187,08 / 179,89 / 178,89 — **4,6 % de espalhamento em código byte a
byte idêntico.**

Esse é exatamente o ruído que §B27 descreve, e a utilidade aqui é que ele
aparece **na mesma execução** que reproduz a fase 4 dentro de 2 %:

- a **etapa** (fase 4) é estável e reproduz entre sessões;
- o **total do ano** oscila o bastante para inverter o sinal de uma comparação.

§B27 chegou a essa conclusão comparando duas árvores e precisou de um controle
para enxergá-la. Aqui ela cai de graça, com uma guarda interna à própria tabela:
a linha de 1 thread é código idêntico quatro vezes, então **qualquer diferença
nela é a régua do ruído** — e 4,6 % de espalhamento ali explica por que os
−3 % e −7 % do ano não significam nada.

**Consequência prática, e é uma regra para o S08:** a sonda já rotaciona a ordem
das variantes (§B27) e ainda assim o total do ano não reproduz. Rotacionar
protege contra viés de posição, não contra variância. Para o S08, a linha de
1 thread de qualquer tabela de variantes deve ser lida **primeiro**, como
medida do ruído daquela execução, antes de qualquer número ser interpretado.

### B4 — o valgrind com threads era barato, e ninguém tinha medido isso também

O buraco nº 3 sobreviveu desde o S05 com uma justificativa implícita de custo —
valgrind é caro, a suíte é a porta de tudo, e o alvo `memcheck` sempre rodou
serial. Medido (§D3): as três variantes sob `--threads 8` custam **7,7 s** no
`demo200`, contra os ~9 s da suíte inteira.

É barato o bastante para entrar na suíte padrão, e foi o que se fez. O achado
menor, mas que vale ao texto: **a decisão de não medir não tinha sido tomada,
tinha sido herdada.** O alvo `memcheck` nasceu no S02, quando não havia thread
nenhuma no programa, e continuou serial por inércia através de dois estágios que
acrescentaram pool, barreira e um vetor de 1024 mutexes — nenhum dos quais ele
jamais viu.

---

## §N notas para o texto

- **As tabelas de §B26 estão liberadas para o texto** (§B2). Era a última coisa
  que o código devia à seção 4: as colunas B e C tinham sido medidas uma vez só,
  e §B25 proíbe afirmar antes de remedir. Remedidas em sessão diferente, com a
  sonda versionada, reproduzem dentro de 2 % nas quatro colunas — incluindo a
  curva de granularidade e a guarda de `PAR_SHARDS=1`.
- **A régua do ruído tem um lugar natural na tabela, e vale explicá-la** (§B3):
  na linha de 1 thread as quatro variantes degeneram no mesmo código serial, e
  ali qualquer diferença **é** ruído, por construção. Mediu-se 4,6 % de
  espalhamento. Isso é o que permite dizer, sem apelar a estatística, por que a
  fase 4 é comparável entre variantes e o total do ano não é — e por que B e C
  aparecem "abaixo do piso" no ano, que seria fisicamente impossível.
- **A dupla metodológica da seção 4 virou trio.** §D3 do S06: a **entrada**
  decide qual gargalo o experimento enxerga. §B27: a **ordem** decide o sinal.
  E agora §B3 daqui: a **granularidade da medida** decide se há sinal — a mesma
  execução reproduz a etapa dentro de 2 % e não reproduz o total. Os três são
  parâmetros do experimento que não estão no código medido.

- **A distinção que este estágio produz, e que a seção 4 precisa fazer:** uma
  suíte verde não diz que o programa está certo, diz que o programa passa nos
  testes que alguém escreveu. As cinco propriedades de §D1 eram verdadeiras e
  não verificadas — e do lado de fora, afirmação verdadeira não verificada e
  afirmação falsa não verificada **são indistinguíveis**. A diferença só aparece
  quando alguém mede, e o custo de descobrir é assimétrico: 40 minutos agora,
  contra descobrir pelo texto depois de publicado.
- **O caso de `par.c:118` é o melhor exemplo didático do projeto**, e é melhor
  que os quatro do padrão "o código calcula e joga fora" (§B8, §B15, §B16, §B17)
  porque não é um defeito do SLEUTH original: é **nosso**, foi escrito neste
  estágio de trabalho, num arquivo que `par.h` manda ler antes de mexer em
  thread, e afirma cobertura de teste que nunca existiu. Quinta ocorrência do
  padrão da armadilha 4 do HANDOFF numa cara nova — não é critério errado nem
  medição isolada, é **cobertura afirmada e não conferida**.
- **§V3 afirmava mais do que a suíte sustentava** (buraco nº 5), e isso foi
  descoberto conferindo o buraco nº 4 em vez de aceitá-lo. Vale como nota
  metodológica: a auditoria que se audita acha mais um.
- **A relação com §B25** fecha um arco do projeto. §B25 nasceu de um achado de
  desempenho que não se reproduziu e virou a regra "remede antes de afirmar".
  Este estágio aplica a mesma regra ao outro lado da casa — **corretude
  afirmada e não medida** — e T0 paga a dívida que a própria §B25 deixou em
  aberto sobre as colunas B e C de §B26.
