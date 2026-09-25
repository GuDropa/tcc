# SPEC — TCC: paralelização do núcleo do SLEUTH com threads POSIX

**Spec mestre.** Estável, raramente muda. Define objetivo, restrições, invariantes e a
ordem dos estágios. Os specs de cada estágio ficam em `specs/` e são escritos
sob demanda, um por vez.

**Se você é um chat novo: comece por `HANDOFF.md`.** Ele é curto, diz o que
ler e o que **não** ler, traz os comandos prontos e lista as armadilhas que já
custaram tempo. Foi feito para você não gastar contexto redescobrindo.

Depois dele, nesta ordem —

1. este arquivo inteiro;
2. o spec do estágio corrente — §T, primeira linha com status `>` ou `.`;
3. `specs/README.md`, a convenção de trabalho;
4. `history/` (o mais recente) — **sob demanda, não por padrão**: conta *como*
   se chegou aqui (decisões, alternativas descartadas, erros cometidos). São
   ~80 KB; abra quando precisar do porquê de algo, não para se situar.

Não releia `plano-tcc-sleuth-paralelo.md` como fonte de verdade — ele é o plano
original do aluno e já contém vários pontos corrigidos por §B deste documento.
Vale como registro de intenção, não como especificação.

---

## §G objetivo

Desenvolver um protótipo em C de simulador de crescimento urbano equivalente ao
SLEUTH, com processamento multicore via threads POSIX, tratando explicitamente os
problemas de exclusão mútua decorrentes do acesso concorrente às matrizes
compartilhadas — e escrever o texto do TCC que documenta esse trabalho.

Dois produtos, entregues juntos: **código** (protótipo funcional + medições) e
**texto** (seguindo o esqueleto do orientador). Entrega ao orientador: 25/09/2026.

---

## §C restrições

**Linguagem e build**
- Núcleo em **C** (C11), não C++. Decisão fechada, o orientador foi explícito.
  Exceção já consumada: o Jogo da Vida preliminar foi feito em C++17 (ver §B1).
- Compilação com `gcc -pthread`. Sem OpenMP, sem MPI, sem dependências externas.
- Sincronização: `pthread_create/join`, `pthread_mutex_t`, `pthread_barrier_t`,
  `sem_t`, e builtins atômicos do GCC (`__atomic_compare_exchange_n`).

**Escopo do protótipo — o que fica de fora**
- Calibração (brute force, Monte Carlo, Lee-Sallee).
- Leitura/escrita de GIF (todo o subsistema `gdif_obj` / `GD/`).
- Interpolação temporal entre anos históricos.
- Modelo Deltatron de cobertura do solo.
- Validação com dados geográficos reais.

O foco é o **desempenho do núcleo**, não a qualidade da previsão urbana.

**Modelo de execução**
- Threads de kernel (NPTL, mapeamento 1:1, futex). N threads disparadas a partir
  de `spr_spread`, particionamento estático.
- Matrizes de dados (`z`, `delta`, `slp`, `excld`, `roads`) **globais**, conforme
  orientação do professor — não parâmetros passados função a função.

**Texto**
- Português brasileiro, formatação ABNT.
- Estrutura fixada pelo esqueleto do orientador (`Esqueleto TCC.txt`). A seção de
  justificativa foi suprimida; a seção 1 termina no objetivo.

---

## §I interfaces

- `I.tree` — árvore de trabalho `sleuth-par/`, separada de `SLEUTH/` (que fica
  intocada como referência e como baseline de comparação).
- `I.cli` — `sleuth-par [--threads N] [--years Y] [--rows R] [--cols C]
  [--seed S] [--variant A|B|C|none] [--dump FILE] [--hash]
  [--load DIR] [--coeff d,b,s,sr,rg] [--critical-slope V]
  [--rng legacy|anchored] [--scan forward|reverse]`
  As três do meio entraram em S03 (T4, T5). `--rng` entrou com S04 (T2), com
  padrão `anchored`; `--repeat N` entra com T8, que foi movida para o S08.
  `--scan` entrou com S04 sem estar previsto aqui: é instrumento de
  verificação de V4 — inverte a ordem das linhas na varredura da fase 4 sem
  mudar o modelo, e sem ele VS17 não seria verificável. Ver §B7 de
  `specs/S04-rng.md`. Continua útil em S05 e S07.
- `I.spread` — `spr_spread(...)`: único ponto de entrada do núcleo. Lê `z`,
  escreve `delta`, faz merge em `z` ao final.
- `I.rng` — `rng.h`: gerador contador-based ancorado à célula, chave
  `(semente, ano, fase, i, j, subpasso)`. Refinamento de S04 (§D4 do spec):
  só a fase 4 varre a grade, então `(i,j)` é a âncora dela; nas fases 1n3 e 5,
  que fazem um número fixo de sondagens aleatórias, a âncora é o **índice da
  iteração**. O que a chave exige é uma coordenada determinística do trabalho,
  não necessariamente uma célula.
- `I.baseline` — `SLEUTH/grow`, o binário original, compilável com o `Makefile`
  que veio no tarball. É a referência normativa de corretude.
- `I.specs` — `specs/`, um arquivo por estágio. Ver `specs/README.md`.

---

## §V invariantes

Numeração monotônica. Nunca reusar um número.

- **V1** — O núcleo é C. Nenhum arquivo `.cpp` entra em `sleuth-par/`.
- **V2** — `z` é somente-leitura durante as três fases. Toda escrita de crescimento
  vai para `delta`. `z` só é modificado no laço de merge, ao final de `spr_spread`.
  *(Double buffering já existe no original — ver §B2.)*
- **V3** — A saída com N threads é idêntica **byte a byte** à saída com 1 thread,
  para todo N, dada a mesma semente. Se essa invariante cair, ela é rebaixada
  para equivalência estatística **e a limitação é declarada no texto**, nunca
  silenciada.

  **Refinada em 20/09 (S05, §B5), e a distinção importa.** "A saída" quer dizer
  a **saída do modelo**: a matriz `z` e as sete estatísticas anuais (`sng`,
  `sdc`, `og`, `rt`, `num_growth_pix`, `pop`, declividade). Essas são idênticas
  byte a byte.

  **Cobertura real, conferida no S07 — e até lá esta linha afirmava mais do que
  a suíte sustentava.** Ela dizia "verificado em 1, 2, 4, 8 e 16 threads", e o
  16 tinha sido rodado à mão no S05, nunca automatizado: VS19 iterava
  `for n in 2 4 8`. Hoje: **VS19** cobre 1/2/4/8; **VS32** cobre 16 e 32, que é
  sobrescrição numa máquina de **6 núcleos físicos** (§B30); **VS28** cobre as
  sementes 2, 7, 42 e
  43, porque até o S07 toda checagem de determinismo paralelo passava por
  `--seed 1`; **VS31** cobre `nthreads > nrows`, em que parte dos workers fica
  com faixa vazia; e **VS24/VS25** cobrem o eixo de variante. Ver §D1 e §B1 de
  `specs/S07-rede-de-verificacao.md`.

  Os cinco contadores de *tentativa* de urbanização que o protótipo imprime
  **não são saída do modelo, e não são todos invariantes**: `urban_success` e
  `z_failure` são exatos, mas a repartição entre `delta_failure`,
  `slope_failure` e `excluded_failure` depende de quem venceu cada disputa — e
  varia inclusive entre execuções com o mesmo N. Só a **soma** das três é
  exata. Isso não é V3 caindo: é a instrumentação medindo a corrida de que ela
  participa. Ver §B21 e §B5 de `specs/S05-varreduras.md`.
- **V4** — O sorteio é ancorado à **célula e ao subpasso**, não à thread. Duas
  execuções com contagens de threads diferentes consomem a mesma sequência para
  a mesma célula. **Verificada em 19/09** (S04, VS17), e verificada *sem
  threads*: inverter a ordem de varredura da fase 4 produz saída idêntica byte
  a byte no modo `anchored` e divergente no modo `legacy`. Refinamento de §D4
  do S04 — em 1n3 e na fase 5 a âncora é o índice da iteração, não uma célula,
  porque nelas não há célula antes do sorteio. E a âncora inclui o índice da
  **tentativa**, não só a origem; ver §B17.
- **V5** — Nenhum contador de estatística é incrementado sem proteção.
  Acumuladores por thread, redução na barreira. Vale para `sng`, `sdc`, `og`,
  `rt`, `num_growth_pix`, `average_slope` e todos os `stats_Increment*`.
- **V6** — A região crítica é o read-modify-write sobre `delta` dentro de
  `spr_urbanize`. As variantes A (mutex global), B (sharding de locks) e C (CAS
  atômico) são selecionáveis **em tempo de execução** pela mesma compilação.

  **Cumprida e verificada em 20/09** (S06, VS24–VS27). As três produzem o mesmo
  `z` byte a byte entre si e contra o serial, em 1/2/4/8 threads; o TSan sai
  limpo nas três e continua acusando `none`; e com uma thread as quatro
  degeneram no mesmo código serial, reproduzindo a saída pré-S05.

  Refinamento que o estágio trouxe (§D4 do spec): **a interface que unifica as
  três é a da operação sobre a célula, não a do mecanismo.** A e B são "entrar
  numa região crítica" com granularidade diferente; C não é região crítica
  nenhuma — é uma operação que toma a célula ou descobre que a perdeu. Daí
  `par_cell_read` / `par_cell_commit` / `par_cell_enter` / `par_cell_leave`, e
  daí um ramo em `spr_urbanize` que é morto sob A e sob B.
- **V7** — A execução serial do protótipo (1 thread) reproduz a saída do SLEUTH
  original para a mesma entrada e a mesma semente. Se divergir, o erro está na
  portagem, não na concorrência. **Verificada em 18/09** (S03, VS11). A partir
  de S04 verifica-se com `--rng legacy`: trocar o gerador muda o fluxo de
  sorteios e portanto os números, por construção e não por erro — os dois
  geradores coexistem justamente para que este oráculo não se perca. Ver §D7
  de `specs/S04-rng.md`.
- **V8** — As fases são sequenciais entre si: `phase1n3` → barreira → `phase4` →
  barreira → `phase5` → barreira → merge. Nenhuma fase começa antes de a
  anterior terminar em todas as threads.
- **V9** — Nenhuma alocação de memória dentro do laço de fases. Buffers
  dimensionados uma vez, na inicialização.
- **V10** — **O código é a referência normativa**, não o artigo de 1997. Onde
  divergirem, o código manda e a divergência é documentada.
- **V11** — Medições de tempo usam `clock_gettime(CLOCK_MONOTONIC)`, excluem I/O
  e geração de dados da região cronometrada, e reportam a **mediana** de
  repetições múltiplas.
- **V12** — Toda divergência entre código e artigo, e todo bug encontrado no
  código original, é documentado em §B e no texto. Nada é corrigido
  silenciosamente.
- **V13** — Texto em pt-BR, ABNT. Toda afirmação numérica sobre o SLEUTH
  (número de cidades, datas, autores) tem fonte citada.
- **V14** — `OFFSET(i,j)` não pode chamar função no caminho quente. Ver §B3.
- **V15** — Grades pequenas (20×20, 100×100) servem **só para corretude**.
  Nenhuma afirmação de speedup é feita sobre elas. Ver §B6.

---

## §T estágios

Status: `.` pendente · `>` em andamento · `x` concluído · `~` concluído com desvio

| id | st | estágio | spec | dia (plano) | cita |
|----|----|---------|------|-------------|------|
| S01 | ~ | Jogo da Vida — valida a arquitetura de threads num AC mínimo | `game_of_life/SPEC.md` | 15/09 | V3,V8 |
| S02 | x | Base serial: árvore `sleuth-par/`, globais, stubs, `main.c`, roda 20×20 | `specs/S02-base-serial.md` | 16/09 | V1,V2,V7,V9,V14 |
| S03 | ~ | Harness: `testgen.c`, semente fixa, dump/hash, baseline cronometrado vs `grow` | `specs/S03-harness.md` | 17/09 | V7,V11,V15 |
| S04 | x | RNG ancorado à célula (`rng.c`/`rng.h`), substituição das macros | `specs/S04-rng.md` | 18/09 | V4,V3 |
| S05 | x | Paralelização das **quatro varreduras `O(N)`** de `spr_spread` (faixas de linhas + barreiras) — recorte ampliado por medição, ver §B19 | `specs/S05-varreduras.md` | 18/09 | V2,V3,V5,V8 |
| S06 | x | Variantes B e C da região crítica + a contagem de população fundida no merge — recorte **arbitrado por medição** em 20/09, ver nota abaixo | `specs/S06-regiao-critica.md` | 19/09 | V3,V5,V6,V11 |
| S07 | x | Rede de verificação: as cinco propriedades que o projeto afirmava por argumento e nunca mediu — recorte **arbitrado por auditoria**, ver nota abaixo | `specs/S07-rede-de-verificacao.md` | 22/09 | V3,V4,V6,V7 |
| S08 | . | Desempenho: grades grandes × {1,2,4,8} threads × 3 variantes — **o instrumento antes da tabela**, recorte arbitrado por medição em 20/09 | `specs/S08-desempenho.md` | 23/09 | V11,V15 |
| S09 | . | Texto do TCC — todas as seções, bibliografia, ABNT | a escrever | contínuo | V13 |

**Nota sobre o S06, a resolver quando ele for escrito (§R1).** A linha acima
dizia "paralelização de `spr_phase1n3` e `spr_phase5`; variantes A, B, C", e
duas coisas a desatualizaram em 19/09:

1. A **variante A** desceu para o S05 (§B19): um estágio que deixasse corrida
   de dados entregaria comportamento indefinido, não uma versão mais lenta.
   Restam B e C ao S06.
2. **Paralelizar as fases 1n3 e 5 provavelmente não se justifica.** As duas
   fazem um número *fixo* de sondagens com dependência entre iterações — a
   fase 5 caminha pela malha viária, e cada passo depende do anterior — e
   juntas valem 1 % dos sorteios (§D1 do S04) e ~11 % do relógio (§B19). O
   candidato real dentro da fase 5 é outro: a **montagem da lista de
   crescimento**, que é `O(total_pixels)` e responde pela maior parte do custo
   dela em grades grandes. Ver §D7 de `specs/S05-varreduras.md`.

Não se reescreve a linha agora porque o resultado do S05 muda o que faz sentido
no S06 — que é exatamente a razão de os specs serem escritos um de cada vez.

**O S05 fechou (20/09) e trouxe o que faltava para decidir.** Três candidatos
ao S06, em ordem de tamanho do efeito medido, para o spec dele arbitrar:

1. **Pool persistente.** §B6 do S05: a 200×200, o `pthread_create` de 7
   threads por ano custa 9,94 ms contra 7,85 ms do ano serial inteiro — o
   `--threads 8` é mais lento que o serial, e a causa é montar o pool, não a
   contenção. Threads criadas uma vez e dirigidas por barreiras entre os anos
   eliminam a coluna. É o maior ganho disponível e o menos arriscado.
2. **Variantes B e C**, que é o que a linha do §T já dizia. §D3 do S05 dá o
   alvo: o mutex global serializa 100 % das urbanizações para proteger ~4 %
   delas. Continua valendo — mas §B22 avisa que o ganho pode ser pequeno se a
   banda de memória já for o gargalo, então **medir antes de implementar as
   duas**.
3. **As duas varreduras `O(N)` que sobraram serial:** a montagem da lista de
   crescimento da fase 5 (§D7 do S05) e a contagem de população (§B24). A
   segunda é trivial — é uma redução, o mesmo padrão do merge. A primeira
   exige concatenação determinística e é a arriscada.

Paralelizar as fases 1n3 e 5 propriamente ditas continua não se justificando,
pelos motivos do item 2 acima.

**Arbitrado em 20/09, e a medição inverteu a ordem dos três candidatos.** O
spec do S06 está escrito (`specs/S06-regiao-critica.md`) e a sondagem que o
precedeu (`harness/probe-s06.sh`) mede, a 2000×2000 com fração urbana real:

| candidato | posição aqui | custo medido a 8 threads |
|---|---|---:|
| variantes B e C | 2º, "medir antes" | **+126 % na fase 4** (32,66 contra 14,63 ms sem trava) |
| lista da fase 5 | 3º | 13,33 ms, 13,2 % do ano |
| pool persistente | 1º, "o maior ganho" | 6,10 ms, 6,0 % do ano |

O pool só domina em grade pequena, onde V15 proíbe afirmação de desempenho. E
o aviso de §B22 — "o ganho de B e C pode ser pequeno se a banda de memória já
for o gargalo" — não se confirmou: a fase 4 é a mais densa em trabalho por byte
das quatro varreduras (§B22 mesmo mediu 3,08× nela) e é **contenção**, não
banda, que a segura.

A inversão tem uma causa só, e ela é o achado metodológico do estágio: **as
entradas que o projeto tinha não continham o gargalo.** A grade artificial toma
a trava 162 vezes em 20 milhões de células varridas (0,000008 por célula, 325×
menos que a real), porque sem cidade a fase 4 curto-circuita e nunca chega à
região crítica; o `demo200` tem a proporção certa mas é pequeno demais para o
efeito aparecer acima do pool. Só a grade **ladrilhada** de §D1 do spec —
`demo200` repetido 10×10, fração urbana idêntica de 9,70 % — tem as duas
coisas. Ver §D3 do spec do estágio.

O recorte escolhido é o **núcleo medido**: variantes B e C (o que V6 exige, e
agora o maior ganho) mais a fusão da contagem de população no merge (12,4 % do
ano por uma comparação dentro de um laço que já existe). Pool persistente e
montagem da lista da fase 5 ficam **declarados fora com o número que os
justifica**, não omitidos.

**S06 concluído (20/09).** As seis tarefas fecharam, T6 com desvio de ordem
(rodou antes do T5, pelo motivo em §T do spec do estágio). **V6 está cumprida:**
as três variantes da região crítica existem, são selecionáveis por `--variant`
em tempo de execução pela mesma compilação, e a interface que as unifica é a da
**operação** sobre a célula — `par_cell_read` / `par_cell_commit` /
`par_cell_enter` / `par_cell_leave` —, não a do mecanismo. O achado de projeto
por trás disso é §D4 do spec: C não é uma região crítica, e forçá-la na
interface de trava daria um `lock()` que não tranca nada.

A contagem de população deixou de ser a sexta varredura `O(N)` e passou a ser
uma comparação dentro do laço do merge (§B24 fica quitado por eliminação).

**A suíte roda VS1–VS27**, com VS12 e VS13 ainda adiadas para o S08. VS24 e
VS25 acrescentam o eixo de variante a V3 e às invariantes de §B21; VS26 exige
TSan limpo em A, B **e C** e continua exigindo que `none` acuse; VS27 exige que
as quatro variantes degenerem no mesmo serial com uma thread.

Os três achados que mudam o texto são **§B26** (a exclusão mútua escolhida é o
que impedia a fase 4 de escalar, e as duas alternativas empatam no piso),
**§B2 do spec do estágio** (a ordem das execuções decide o sinal do resultado —
irmão metodológico de §D3) e **§B6 do spec** (a curva de granularidade da
exclusão, de 1 a 16384 travas).

O calendário segue no lugar: o S06 era do dia 19 no plano e fechou no 20.
A quarta 24/09 continua sendo colchão — e o **texto continua em zero**, que é a
dívida que sobrou sozinha no topo.

**Nota sobre o S07, a resolver quando ele for escrito (§R1).** A linha acima
dizia "corretude e determinismo: 1 vs 2 vs 4 threads", e o S05 e o S06 a
esvaziaram: isso é VS19, VS20, VS22, VS24, VS25 e VS27, todas automatizadas e
rodando em 9 s. Escrever o S07 como a linha o descreve produziria verificação
que já existe.

**Auditoria de 20/09, feita em vez de supor.** O que falta é outra coisa, e os
quatro itens têm a mesma forma — **propriedades que o projeto afirma por
argumento e nunca mediu**, dentro da própria rede de verificação:

1. **V3 sob threads só foi verificada com uma semente.** Todas as checagens de
   determinismo paralelo passam por `$REF`, que é `--seed 1`
   (`tests/run_checks.sh:416`).
2. **O `--scan reverse` nunca rodou com threads** (`run_checks.sh:377-379`). O
   HANDOFF argumenta que VS17 "vale palavra por palavra paralelizado"; é
   argumento, não medida — e é o instrumento que o projeto elegeu como primeiro
   diagnóstico.
3. **O valgrind nunca rodou com threads, nem sob B ou C.** O alvo `memcheck`
   não passa `--threads` nem `--variant` (`Makefile:66-68`), então o vetor de
   1024 mutexes do S06 nunca foi alocado sob valgrind.
4. **`par.c:118` documenta uma garantia inexistente:** diz que a faixa vazia é
   exercitada "numa grade 20x20 com `--threads 32`, que os testes exercitam", e
   nenhum teste usa mais de 8 threads.

O quarto é o mais instrutivo e deve virar §B do estágio: é **cobertura afirmada
e não conferida**, no arquivo que `par.h` manda ler antes de mexer em thread.
Quinta ocorrência do padrão da armadilha 4 do HANDOFF, numa cara nova.

**S07 concluído (20/09).** As seis tarefas fecharam. O spec é
`specs/S07-rede-de-verificacao.md`, e ele **ampliou a auditoria acima**: ao
conferir o item 4 em vez de aceitá-lo, apareceu um quinto buraco — **§V3 e §B21
deste arquivo afirmavam, os dois, que V3 fora verificada "em 1, 2, 4, 8 e 16
threads", e VS19 iterava `for n in 2 4 8`.** O 16 foi rodado à mão no S05 e
nunca virou suíte. O item 4 mente num comentário; o item 5 mentia num
**invariante**.

**Os cinco foram sondados antes de catalogados (§R1), e os cinco passam.** A
auditoria não achou defeito nenhum: achou **afirmação verdadeira e não
verificada**. Isso muda o que o estágio entrega — não é conserto, é converter
argumento em medida — e é o material de texto que ele produz (§N do spec): do
lado de fora, afirmação verdadeira não verificada e afirmação falsa não
verificada são indistinguíveis.

A suíte passou a rodar **VS1–VS32**, com VS12 e VS13 ainda adiadas para o S08.
VS28 põe as outras quatro sementes sob threads; VS29 leva o `--scan reverse` a
2/4/8 threads; VS30 roda valgrind **com** 8 threads nas três variantes, que
nunca tinha acontecido — o vetor de 1024 mutexes da variante B jamais fora
alocado sob memcheck; VS31 exercita a faixa vazia de verdade (20×20 com 16, 32 e
64 threads); VS32 cobre a sobrescrição que §V3 afirmava.

O calendário continua no lugar: o S07 era do dia 22 no plano e fechou no 20.
Restam o S08 e o texto — e **o texto é agora a única dívida grande**, sem
nenhuma pendência de medição bloqueando a seção 4.

**Estado real em 18/09/2026:** S01 concluído (com desvio, §B1). S02 concluído:
`sleuth-par/` compila limpo, roda 5 anos em 20×20, valgrind sem erro nem
vazamento, determinismo entre execuções confirmado.

S03 **concluído com desvio (`~`).** Feitas T1–T6, T9, T10 e T12. O harness
converte as grades do `demo200` de GIF para texto
(`sleuth-par/data/demo200/`), constrói um `grow` de referência durável a partir
de uma cópia de `SLEUTH/`, e gera o cenário de referência; o `sleuth-par`
ganhou `--load`, `--coeff` e `--critical-slope`. Verificações VS8–VS11 e VS14
passando, **todas automatizadas** em `tests/run_checks.sh`.

**O desvio:** T7 (`testgen` que escale), T8 (cronometragem V11) e T11
(`baseline.md`) foram **movidas para o S08**, levando junto VS12 e VS13. As
três são infraestrutura de *benchmark* e nada em S04–S07 depende delas — quem
depende é o S08. A alternativa era gastar horas do caminho crítico da
paralelização, que é o que está atrasado, produzindo artefatos que ficariam
parados até o dia 23. O `run_checks.sh` imprime VS12 e VS13 como **adiadas**,
em vez de omiti-las, para que o desvio não vire esquecimento (§V12).

A suíte completa roda em **9,4 s** — antes do S03 levava cerca de um minuto, e
a maior parte era o VS7 recompilando o SLEUTH inteiro num diretório temporário
que depois apagava. Agora ele confere o baseline durável, que é o binário que
a comparação de V7 de fato usa.

**V7 está verificada** (18/09). `tests/compare_v7.sh`, ou `make v7`, roda o
protótipo serial e o `grow` original em 1990→2010 sobre o `demo200` com semente
1, e compara sete estatísticas por ano: `sng`, `sdc`, `og`, `rt`,
`num_growth_pix`, `pop` e a declividade média da área urbana. As seis primeiras
batem **exatamente** nos 20 anos; a sétima bate dentro das duas casas decimais
que o `avg.log` publica. A superfície de comparação é a saída textual que o
original já escreve — nada foi instrumentado do lado dele.

Isso faz do S03 o estágio que transformou V7 de aposta em fato, e portanto
firma o chão de V3: daqui para a frente, divergência entre 1 e N threads é
problema de concorrência, não de portagem.

**S04 concluído (19/09).** As sete tarefas fecharam, T4 com desvio pequeno
(§B1 do spec do estágio). O núcleo passou a ter dois geradores selecionáveis em
tempo de execução: `--rng legacy`, o `ran1` original, que preserva V7; e
`--rng anchored`, padrão, em que o sorteio é função pura da chave
`(semente, ano, fase, a, b, subpasso)` — sem estado, sem trava, sem
armazenamento por thread. O `static last_index` de `util_get_next_neighbor`
virou cursor na pilha do chamador. Dos quatro estados compartilhados
catalogados em §B12, **dois foram eliminados** e sobrou um só precisando de
exclusão mútua (o RMW sobre `delta`, de S06).

**V4 está verificada, e sem threads** — VS17. Inverter a ordem de varredura da
fase 4 dá saída idêntica byte a byte em `anchored` e divergente em `legacy`. É
o teste que S05 precisava que existisse *antes* de haver concorrência: se isso
falhar paralelizado, a causa é corrida de dados e não ordem de consumo, e as
duas têm soluções diferentes.

**V7 continua verificada**, agora com `--rng legacy` explícito no
`compare_v7.sh`. VS18 é a rede que separa os dois efeitos: o dump de `legacy`
depois do S04 é idêntico byte a byte ao dump colhido antes de T3, o que prova
que a propagação da chave não alterou o modelo por acidente.

A suíte passou a rodar **VS1–VS18** em cerca de 11 s, com VS12 e VS13 ainda
adiadas para o S08.

O calendário ficou no lugar: S04 era do dia 18 e fechou na madrugada do 19.
A quarta 24/09 continua sendo colchão.

**S05 concluído (20/09).** As oito tarefas fecharam na ordem prevista. As
quatro varreduras `O(N)` de `spr_spread` rodam em N faixas de linhas
coordenadas por barreiras, `--threads N` vale, e a região crítica de
`spr_urbanize` está protegida pela variante A (mutex global), que desceu do
S06 por §B19. `par.c`/`par.h` são o pool, o particionamento e a redução;
`spr_spread` deixou de executar as etapas e passou a disparar `par_run`.

**V3 está verificada sob threads** (VS19): 1, 2, 4, 8 e 16 threads produzem o
mesmo `z` byte a byte em 20 anos de `demo200`, e a tabela anual inteira é
idêntica. **VS21 fecha o outro lado**: `--threads 1` reproduz bit a bit a saída
colhida antes do estágio, nos dois modos de RNG — o que separa "mudou porque
paralelizei" de "mudou porque errei ao reorganizar", e o estágio reorganizou
bastante.

**VS23 é nova e ganhou dentes.** O ThreadSanitizer sai limpo na variante A e
**acusa** na `none`, nomeando as duas linhas do read-modify-write. Isso
importa porque §B10 do spec do estágio mediu o cenário que se temia: com
`none` e 8 threads, 6 de 6 execuções deram o `z` certo e o programa continuava
com corrida de dados. V3 e TSan medem coisas diferentes e as duas são
necessárias.

Os três achados que mudam o texto são §B21 (a exclusão mútua não torna a
instrumentação independente da ordem), §B22 (o teto é banda de memória, não
Amdahl) e §B23 (paralelizar o resto piora a parte serial). Os dois últimos
reescrevem o argumento de §4.2.

A suíte passou a rodar **VS1–VS23**, com VS12 e VS13 ainda adiadas para o S08.
O calendário segue no lugar: o S05 era do dia 18 no plano e fechou no 20,
dentro do colchão.

Correção à leitura do S02: dizer que "o `grow` original foi compilado como
baseline" era enganoso. O S02 provou que o original **compila**, mas o binário
era construído num diretório temporário e descartado; e o `SLEUTH/grow` da
árvore é o ELF MIPS de 2001 do tarball. Além disso o original **não roda até o
fim** num toolchain moderno sem um patch de um byte. Ver §B1 e §B6 de
`specs/S03-harness.md`.

O atraso de um dia foi recuperado — S02 era do dia 16 e fechou no 18, mas o
dia 17 rendeu o levantamento que virou §B4–§B13. A quarta 24/09 continua
sendo colchão.

**Ambiente de execução:** WSL Ubuntu 24.04, gcc 13.3, valgrind 3.22. Compilar e
rodar sempre dentro do WSL — o MinGW do Windows não é alvo.

**AMD Ryzen 5 4600G: 6 núcleos físicos, 12 lógicos (SMT 2× ligado)**, um
soquete, um nó NUMA, L3 de 4 MiB compartilhado entre os seis. Conferido com
`lscpu` em 20/09; os irmãos SMT pareiam `0/1`, `2/3`, … `10/11`, então os
físicos são os lógicos pares. 7 GiB de RAM para o WSL.

⚠ **Esta linha dizia "12 núcleos" e afirmava que eles "cobrem confortavelmente
a matriz {1,2,4,8} threads de S08". As duas coisas estavam erradas** — ver §B30.
A 8 threads a matriz já excede os 6 físicos. A frase antiga fica registrada aqui
porque §V12 proíbe correção silenciosa.

---

## §B achados e correções ao plano original

Cada linha aqui corrige ou complementa `plano-tcc-sleuth-paralelo.md`. Ordem de
descoberta.

| id | data | achado | consequência |
|----|------|--------|--------------|
| B1 | 17/09 | O Jogo da Vida (§8.4 do plano, "em C") foi implementado em **C++17** com CMake, `std::thread`, dual buffer e faixas de linhas. Está completo e com teste de equivalência serial×paralelo passando. | Aceitável: é teste didático preliminar e alimenta §3.0 do texto. **Não** serve de base de código para o SLEUTH. V1 continua valendo para `sleuth-par/`. Declarar a escolha no texto. |
| B2 | 17/09 | O **double buffering já existe** no SLEUTH original: as três fases leem `z` e escrevem só em `delta`; `z` muda apenas no merge. | Não é preciso criar buffer de saída. O particionamento por faixas não gera corrida de leitura: `z` é imutável durante as fases. É o achado que torna o trabalho viável no prazo. → V2 |
| B3 | 17/09 | `OFFSET(i,j)` em `ugm_macros.h` é `((i)*igrid_GetNumCols() + (j))` — **uma chamada de função por indexação**. | No laço quente isso é caro e amarra o núcleo ao objeto `igrid`. Na portagem: `ncols` vira global imutável e `OFFSET` passa a usá-lo direto. Medir o impacto — vira argumento no capítulo de desempenho. → V14 |
| B4 | 17/09 | `PIXEL` é `long` (8 bytes), não `unsigned char`. O trecho da variante C no plano (§7) usa `unsigned char expected = 0` — **está errado**. | O CAS deve operar sobre `PIXEL`/`long`. Falso compartilhamento: 8 células por linha de cache de 64 B, não 64. Corrigir antes de escrever S06. |
| B5 | 17/09 | `ran_random` (Numerical Recipes `ran1`) guarda estado em `static RANDOM_SEED_TYPE iv[32]` e `static iy` **dentro da função**, além do `ran_seed` global. | O gerador é **não reentrante** por construção — duas threads corrompem a tabela de embaralhamento, não só a sequência. O argumento de §4.5 do plano é mais forte do que ele mesmo afirma. Usar no texto (§2.2 e §3.2). |
| B6 | 17/09 | Grades pequenas não mostrarão speedup: criação de threads e contenção dominam. | 20×20 e 100×100 só para corretude. Desempenho exige 1000×1000+ e/ou muitos anos. Declarar honestamente no texto. → V15 |
| B7 | 17/09 | Em `spr_phase5`: `growth_row = (int*)workspace; growth_col = (int*)workspace + nrows;`. A lista de crescimento pode ter até `total_pixels` entradas, mas `growth_col` começa apenas `nrows` inteiros adiante. | **Bug latente no SLEUTH original** — confirmado idêntico em `spread-simplified.c:281` e `SLEUTH/spread.c:408`. Se `growth_count > nrows`, `growth_row` sobrescreve `growth_col`. Numa grade 20×20 com crescimento moderado isso dispara. Corrigir na portagem (alocação separada), documentar como achado no texto, e lembrar que o baseline `grow` carrega o bug — pode explicar divergência em V7. |
| B8 | 17/09 | A macro `URBANIZE` em `ugm_macros.h` testa `RANDOM_FLOAT < swght[...]`, mas `spr_urbanize` (código realmente executado) testa `RANDOM_FLOAT > swght[...]`. | A macro é código morto, não usada por `spread.c`. Não confundir na leitura. Entra na tabela de divergências de §3.1 como curiosidade, com a ressalva de que não afeta a execução. |
| B9 | 17/09 | **O código-fonte completo do SLEUTH 3.0beta_p01 está disponível** em `SLEUTH/` (extraído do tarball do Project Gigalopolis). | Muda três coisas do plano: (a) §6.2 "escrever `globals.h`" é largamente desnecessário — `ugm_defines.h`, `ugm_macros.h` e `random.c` são reais e reaproveitáveis; (b) a questão em aberto §12.2 está resolvida — trabalha-se sobre a árvore original; (c) o teste de §8.1 ("comparar com o original não modificado") passa a ser **executável de fato**, compilando `grow`. Reduz muito o risco do dia 16. |
| B10 | 17/09 | `spread-simplified.c` **não compila como está**: os protótipos de `spr_road_walk`, `spr_urbanize_nghbr` e `spr_get_neighbor` perderam o `static` que existe no original, enquanto as definições continuam `static` (erro em C); e o arquivo usa `bool`, que não está definido em `ugm_defines.h`. | Conserto trivial, detalhado em `specs/S02-base-serial.md` §D2. Confirma que o risco "alto" atribuído ao dia 16 no plano estava superdimensionado. |
| B11 | 17/09 | A lista de funções externas de §6.3 do plano está incompleta: faltam `coeff_GetCurrentSlopeResist` e `scen_GetCriticalSlope`, ambas usadas por `spr_get_slp_weights`. São 25 símbolos, não 23. | Lista completa e conferida em `specs/S02-base-serial.md` §D1. |
| B12 | 18/09 | `util_get_next_neighbor` (`SLEUTH/utilities.c:602`) guarda `static int last_index` — a sequência de vizinhos é estado global mutável entre chamadas. Consumida por `spr_get_neighbor` e `spr_road_walk`, ou seja, pelas fases 1n3 e 5. | **Segundo ponto não reentrante do núcleo**, ao lado do `ran_random` (B5). É importante porque **não está coberto pela região crítica de B3 do plano**: aqui não há escrita em grade nenhuma e ainda assim há corrida de dados e quebra de determinismo. O plano tratava a exclusão mútua como um problema de um ponto só (o RMW sobre `delta`); são pelo menos três (`delta`, `ran_random`, `last_index`). ~~Tratamento em S05/S06 — provavelmente estado por thread.~~ **Resolvido em S04, e não com estado por thread:** o cursor virou `int *` do chamador (T5), e o `ran_random` foi substituído por função pura da chave. Nenhum dos dois precisou de trava. Dos quatro pontos da taxonomia, sobrou **um** exigindo exclusão mútua — o RMW sobre `delta`, de S06. Descobriu-se ainda que o `last_index` era global **sem precisar ser**: ele nunca carregava informação entre rodadas (§B3 de `specs/S04-rng.md`). Enriquece §2.2 e §3.2 do texto: o exemplo didático deixa de ser um contador e passa a ser uma taxonomia de estados compartilhados — em que três dos quatro itens precisavam de projeto, não de sincronização. |
| B13 | 18/09 | A globalização das matrizes **eliminou 6 dos 25 símbolos externos** (os `igrid_Get*GridPtr`, `igrid_GridRelease`, `mem_GetWGridPtr`, `mem_GetWGridFree`), em vez de transformá-los em stubs. | O pool de grades do `memory_obj` original é estado global mutável com contador de empréstimos — sob N threads exigiria trava própria. A globalização remove o ponto de sincronização em vez de sincronizá-lo. Argumento para §3.2. Detalhe em `specs/S02-base-serial.md` §BS6. |
| B14 | 18/09 | **O SLEUTH original não roda até o fim num toolchain moderno.** `util_WriteZProbGrid` (`utilities.c:798`) declara `char date_str[4]` e faz `sprintf (date_str, "%u", ano)` — `"1991"` + NUL = 5 bytes. Estouro de pilha de um byte em **todo** ano de quatro dígitos, e o `_FORTIFY_SOURCE` do glibc aborta. A chamada é incondicional (`growth.c:594`): não há contorno por cenário. | Promovido de `specs/S03-harness.md` §B1 porque **é a primeira parede em que qualquer um esbarra** ao tentar rodar o original — antes de qualquer discussão de modelo. O baseline de referência só existe porque a cópia em `harness/build/sleuth-ref/` leva um patch de um caractere (`[5]`). Não é nota de portabilidade: é estouro de pilha que sempre esteve lá. |
| B15 | 18/09 | **A coluna `sdc` do `avg.log` é sempre 0, e o valor de `sdc` sai na coluna `sdg`.** `growth.c:213-216` chama `stats_SetSNG`, `stats_SetSDG(sdg)`, **`stats_SetSDG(sdc)`**, `stats_SetOG` — `stats_SetSDC` **não existe** (`stats_obj.h:75`). E `sdg` é local que `spr_spread` nunca escreve. | Promovido de `specs/S03-harness.md` §B5. É a armadilha nº 1 de qualquer comparação contra o `avg.log`, incluindo as de S07 e S08: quem mapear `sdc → sdc` compara contra zero e persegue um bug inexistente. No `demo200` ambas as colunas dão 0,00 e o defeito fica invisível. Mapeamento completo em §D5 do spec do S03. |
| B16 | 18/09 | **A coluna `slope` do `avg.log` não é o `average_slope` que `spr_spread` devolve.** `growth.c:204` recebe esse valor e nenhum `stats_Set*` o consome — é saída morta. A coluna publicada vem de `stats_circle` (`stats_obj.c:2653-2677`): média da declividade sobre a área urbana **inteira**, não sobre as células que cresceram no ano. | Promovido de `specs/S03-harness.md` §B11. Foi a **única divergência real** que a verificação de V7 acusou, e custou quase um dia de depuração falsa — o spec do estágio mandava comparar o número errado. O `sleuth-par` imprime os dois: `cresc_decl` (o de `spr_spread`) e `decl_z` (o comparável). Terceiro item do padrão **"o código calcula e joga fora"**, com B15 e com a macro `URBANIZE` morta (B8) — e é nesse subgrupo que a verificação tropeça. |

| B17 | 19/09 | **O laço de `tries` de `spr_phase5` chama `spr_urbanize_nghbr` três vezes com argumentos idênticos.** A origem nunca é atualizada — o laço só escreve em `i_rd_end_nghbr_nghbr`, que é descartado. No original as três tentativas diferem apenas porque `spr_get_neighbor` sorteia. O mesmo padrão, mais brando, está no laço de 8 tentativas da fase 1n3. | Promovido de §B2 de `specs/S04-rng.md`. **É o modo de falha mais traiçoeiro que o projeto encontrou até agora**, e quem mexer em `spr_urbanize` em S05/S06 precisa conhecê-lo: ancorar o sorteio nos argumentos — a leitura ingênua de "ancorado à célula" — faria as três tentativas ficarem bit a bit idênticas, o modelo perderia duas das três, e **nada acusaria**, porque o programa continuaria determinístico e V3 continuaria valendo. A âncora correta é `(fase, origem, tentativa)`. Também é o quarto caso do padrão "o código calcula e joga fora" (com B8, B15, B16): `urbanized` é atribuído e nunca lido nesse laço. |
| B18 | 19/09 | **O gerador ancorado é ~6 % mais RÁPIDO que o `ran1`, não mais caro.** Medido em 10⁷ chamadas, gcc 13.3 `-O2`, três repetições: 5,91/6,01/5,73 ns contra 6,22/6,22/6,27 ns. Isso apesar de fazer mais multiplicações. O `ran1` tem dependência serial entre chamadas (cada uma lê e escreve `ran_seed` e a tabela `iv[32]`); o `rng_double` é puro, então chamadas consecutivas são independentes e o processador as sobrepõe. | Promovido de §B5 de `specs/S04-rng.md`. Contraria a previsão do plano e do próprio §N do S04, que davam o gerador ancorado como custo a declarar. **Muda o argumento de §4.2 do texto:** o determinismo não foi pago em desempenho, foi pago em projeto — e o efeito colateral num único núcleo é positivo. A propriedade que torna o gerador paralelizável já se mede antes de existir qualquer thread. No programa inteiro a diferença some (~20 ms nos dois modos para 20 anos de `demo200`), porque os 106 236 sorteios somam menos de 1 ms. |

| B19 | 19/09 | **A fase 4 é 51,7 % do tempo de `spr_spread`, não a totalidade — e há outras três varreduras `O(total_pixels)` ao lado dela.** Medido em `demo200`, 20 anos, com `harness/probe-s05.sh`: zerar `delta` 5,6 %, fase 1n3 0,7 %, **fase 4 51,7 %**, fase 5 10,2 %, filtros 15,2 %, merge 16,6 %. Há ainda uma quarta varredura `O(N)` escondida *dentro* da fase 5 — a montagem da lista de crescimento —, que é por que essa fase escala com a grade apesar de fazer um número fixo de sondagens. | **Amplia o recorte do S05.** Paralelizar só a fase 4, como esta tabela §T dizia, poria um teto de Amdahl de **2,07×** sobre o trabalho inteiro; incluindo as quatro varreduras `O(N)` o teto vai a ~9,2×. É a diferença entre um capítulo de desempenho e uma nota de rodapé. O S05 passa a entregar também a variante A (mutex global), porque um estágio que deixasse corrida de dados entregaria comportamento indefinido, não "uma versão mais lenta" — B e C continuam no S06. Detalhamento em `specs/S05-varreduras.md` §D1–§D7. |
| B20 | 19/09 | **O gerador artificial não produz cidade grande, e isso falsifica a nota de desvio do S03.** A 1000×1000, mesmo simulando 150 anos, a grade chega a **0,28 %** de área urbana, contra 14,2 % do `demo200`. Como a fase 4 curto-circuita em célula não urbana, toda medição de perfil em grade artificial grande mede uma grade *vazia*, não uma grade *grande*. | O desvio do S03 afirma que "nada em S04–S07 depende" de T7 (`testgen` que escale) e VS13. **Falso para qualquer afirmação de desempenho.** Sem fração urbana realista em grade grande, V15 proíbe afirmar speedup e o S08 não tem sobre o que medir. Não se puxa T7 para agora — o S05 se projeta sobre o `demo200`, que é dado real —, mas o risco fica registrado aqui para que o S08 não seja pego de surpresa. Alternativa barata já levantada, a decidir no S08: ladrilhar o `demo200` até 2000×2000, o que preserva fração urbana e textura sem escrever gerador novo. **Qualificado em 20/09 por §B24: vale para a fase 4, não para as outras três varreduras.** |
| B21 | 20/09 | **A exclusão mútua torna o MODELO independente da ordem, não a INSTRUMENTAÇÃO.** Com a variante A, `z` e as sete estatísticas anuais são idênticos byte a byte em 1, 2, 4, 8 e 16 threads — §D6 do S05 estava certo. Mas os cinco contadores de tentativa de urbanização não: quem perde a corrida por um alvo é contado em `delta_failure` se chegou depois do vencedor e em `slope_failure`/`excluded_failure` se chegou antes e reprovou sozinho. A repartição varia **entre execuções com o mesmo N**; a soma das três é exata (766 em todas as configurações medidas), e `urban_success` e `z_failure` são exatos. | **Refina V3** — ver a nota lá. Não é V3 caindo: a saída do modelo é bit a bit idêntica, e o que varia é a instrumentação, que mede a corrida de que participa. Muda VS20, que passa a verificar as invariantes certas em vez de cobrar uniformidade inexistente — cobrar os cinco idênticos faria a suíte reprovar código correto (armadilha 4). Consertar exigiria reproduzir a ordem global de varredura, que é exatamente o que o particionamento destrói. **Material de primeira para §3.3 e §4.1 do texto.** Detalhe em §B5 de `specs/S05-varreduras.md`. |
| B22 | 20/09 | **O teto das varreduras paralelizadas é a banda de memória, não Amdahl.** A 2000×2000 e 8 threads, as quatro varreduras ganham **2,24×**, contra os 9,2× que §B19 previu por Amdahl. Abrindo por varredura, o ganho cresce com o trabalho por byte lido: zerar `delta` (escrita pura) 2,06×, merge 2,03×, filtros 2,59×, fase 4 (vizinhança + sorteio) 3,08×. É assinatura de saturação de banda, não de contenção nem de desbalanceamento. | **Reescreve o argumento de §4.2 do texto.** A conta de Amdahl de §B19 está aritmeticamente certa e é uma previsão ruim: ela supõe que as varreduras paralelizam perfeitamente, e três das quatro são *memory-bound*. O limite do trabalho não é "quanto sobrou de serial", é "quanto a máquina consegue ler por segundo". Medir a banda da máquina no S08 para publicar a conta fechada. Detalhe em §B7 de `specs/S05-varreduras.md`. |
| B23 | 20/09 | **Paralelizar as varreduras deixa as fases SERIAIS mais lentas.** A 2000×2000, de 1 para 8 threads: fase 5 +45 %, contagem de população +43 %. A `pop` é o controle limpo — medida fora de qualquer barreira, depois do join —, então o número não pode ser espera disfarçada: é o mesmo laço serial 43 % mais lento porque oito threads acabaram de percorrer a grade. Explicação provável (não confirmada, não se mediu contador de cache): localidade. | Custo que nenhuma conta de Amdahl prevê — a fração serial não só deixa de acelerar, **piora** quando o resto acelera. Se confirmar no S08, é argumento forte para o candidato de §D7 do S05 (paralelizar a montagem da lista de crescimento da fase 5), não pelo ganho da etapa mas para não pagar a volta. Também é a segunda razão, junto com §B22, para o S06 olhar o pool persistente. |
| B24 | 20/09 | **Há uma quinta varredura `O(total_pixels)` em `spr_spread`, e a sonda de §B19 não a media.** `util_count_pixels` para a população, no fim da função, ficava fora da marcação — o denominador de §B19 era menor que o ano real. Medida: 6,1 % do ano no `demo200`, 10,0 % a 2000×2000. **E §B20 é mais estreito do que dizia:** a fração urbana só afeta a fase 4, que curto-circuita; zerar `delta`, os filtros e o merge fazem o mesmo trabalho por pixel numa grade vazia, então grade artificial grande é dado legítimo para três das quatro varreduras. | A quinta varredura não muda o recorte do S05 (§S nomeia quatro) e é candidata ao S06 junto com a montagem da lista de crescimento. O segundo ponto reduz — não quita — a dívida de §B20: foi o que permitiu medir §B22 e §B23 sem esperar o `testgen` do S08, mas afirmar speedup do trabalho continua exigindo fração urbana realista. Terceira ocorrência do padrão "a sonda mede o que alguém lembrou de marcar", com o `delta` de §D2 do S05. |

| B25 | 20/09 | **§B23 não se reproduz, e a entrada que faltava existe.** Duas coisas, medidas pela mesma sondagem (`harness/probe-s06.sh`, e o remedido com `probe-s05-threads.sh` sem modificação). **(a)** §B23 afirma que paralelizar as varreduras deixa as fases seriais mais lentas — `pop` +43 %, fase 5 +45 % — e elege `pop` como controle limpo. Remedida no mesmo instrumento e na mesma grade: `pop` vai de 11,54 a 11,45 ms de 1 para 8 threads, **−1 %**, e fica plana em três grades. A fase 5 sobe 8 %, num trecho de sub-milissegundo que cresce quando a grade *encolhe* — assinatura de barreira, não de cache. Todos os absolutos da sessão de §B23 são 25–30 % mais altos que os de agora na mesma configuração, o que aponta carga de fundo. **(b)** Ladrilhar o `demo200` 10×10 (`harness/mktiled.sh`) preserva a fração urbana **exatamente** — 9,70 % nos dois — e V3 vale na grade resultante. O artefato é a costura: as estradas não se conectam entre ladrilhos, o que afeta o modelo e não o perfil (a caminhada pela estrada é 1 % da fase 5). | **(a) retira §B23 do texto**, e §B22 (banda de memória) **se reproduz** e continua de pé. Tira também a "segunda razão" que §B23 dava ao pool persistente e o argumento que dava à lista da fase 5 — os dois seguem justificáveis por tamanho, não por esse efeito. A regra que fica, e que o projeto aprendeu tarde: **achado de desempenho se remede em outra sessão antes de virar afirmação** — mediana protege contra oscilação dentro de uma sessão, não contra uma sessão inteira mais lenta. Quarta ocorrência do padrão da armadilha 4 do HANDOFF, do lado contrário: medição isolada virando achado documentado. **(b) reduz §B20 de novo**, depois de §B24: agora há fração urbana realista em grade grande, que é o que permitiu medir a região crítica (§D3 do S06). Não quita: o ladrilho não dá uma cidade *diferente*, então o `testgen` de T7 continua devido para variar a entrada. |

| B26 | 20/09 | **Não era a fase 4 que não escalava: era a trava escolhida para protegê-la. E as duas alternativas empatam.** Medido no S06 (T5), grade ladrilhada 2000×2000, ordem das variantes rotacionada a cada repetição. Fase 4 com 8 threads: **A 35,48 ms, B 15,48, C 15,25**, contra 15,40 do piso (`none`, o mesmo binário sem exclusão). A trava global custa **+130 %**; o sharding e o CAS custam **+1 %** e **−1 %** — isto é, encostam no piso. No ano inteiro, 103,89 ms sob A contra 77,17 sob B e sob C. E o sintoma que fecha o argumento: **sob A, ir de 4 para 8 threads PIORA** a fase 4 (32,11 → 35,48) e o ano (89,68 → 103,89), enquanto sob B e C os dois continuam caindo — a escalada de 1 para 8 threads do ano é 1,70× sob A e 2,27×/2,29× sob B/C. A granularidade da exclusão é um botão contínuo e mediu-se a curva inteira: 1 trava +146 %, 8 travas +19 %, 64 +6 %, 1024 +1 %, 16384 +3 % (satura e começa a custar cache). A linha de 1 trava é a guarda da tabela — com uma trava só, B *é* A, e custa o que A custa. | **Cumpre V6 e reescreve o §4.2 do texto.** Três consequências. **(a)** O aviso de §B22 — "o ganho de B e C pode ser pequeno se a banda de memória já for o gargalo" — **não se confirmou**: a fase 4 é a mais densa em trabalho por byte das quatro varreduras e o que a segurava era contenção, não banda. §B22 continua valendo para as outras três. **(b)** §D3 do spec do S06 (+126 % em 19/09) foi **remedido em outra sessão** e deu +130 %: passou no teste de §B25, e é o segundo achado do projeto a passar, depois de §B22. ~~As colunas de B e C são de uma sessão só e ainda devem a remedição.~~ **Pagas em 20/09 — ver §B29: as quatro colunas reproduzem dentro de 2 %.** **(c)** O empate entre B e C é o achado mais útil ao texto: **o mecanismo mais sofisticado não foi o mais rápido**, então a escolha entre eles não é de desempenho e sim de modo de falha — B erra por desempenho (poucas travas), C erra por corretude (CAS na largura errada, §B4). Detalhe em §B4–§B6 de `specs/S06-regiao-critica.md`. |
| B27 | 20/09 | **A ordem das execuções decide o sinal do resultado, e quase inventou um achado.** Medindo o ganho do T1 do S06 (a contagem de população fundida no merge), a leitura pelo TOTAL do ano dizia que a mudança havia **piorado** o programa em 4 e 8 threads. A sonda ganhou então um **controle**: a soma das etapas que a mudança não tocou, que é código byte a byte idêntico nos dois binários e portanto tem de dar igual. Deu 8 a 11 ms de diferença. Invertendo qual binário roda primeiro em cada par, o sinal **inverte** (−5 a −19 ms): o que se media era a posição na sequência, não a árvore. O estimador local — a etapa que sumiu menos o que o merge ganhou — é estável nas duas ordens (9,3/11,8/13,5/13,6 contra 10,2/13,3/12,7/13,6 ms). | **Irmão metodológico de §D3 do S06, e a dupla é o melhor material de §4.2.** §D3 diz que a **entrada** decide qual gargalo o experimento enxerga; §B27 diz que a **ordem** decide o sinal. Os dois são parâmetros do experimento que não estão no código medido. Consequências práticas, já aplicadas: comparações entre duas árvores usam **estimador local**, nunca razão entre totais; e a sonda de variantes do T5 **rotaciona a ordem** a cada repetição, senão a variante medida primeiro ganharia de graça — e `none`, que é o piso contra o qual as outras são julgadas, sairia penalizado. É também um **mecanismo candidato para §B23**, cuja sonda percorria 1, 2, 4, 8 threads nessa ordem e cujo número saiu da última configuração da sequência; fica como hipótese com o teste que a decide (embaralhar a ordem da matriz), não como explicação estabelecida. |

| B28 | 20/09 | **Cinco propriedades que o projeto afirmava por argumento e nunca mediu — e as cinco são verdadeiras.** Auditoria da suíte feita no S07, conferida com `file:line` e depois **sondada antes de catalogada**: (1) toda checagem de determinismo paralelo passava por `--seed 1`; (2) o `--scan reverse` — o instrumento que o HANDOFF elege como primeiro diagnóstico — nunca rodara com threads; (3) o valgrind nunca rodara com threads nem sob B ou C, de modo que o vetor de 1024 mutexes do S06 jamais fora alocado sob memcheck; (4) `par.c:118` afirmava que a faixa vazia é exercitada "numa grade 20x20 com `--threads 32`, que os testes exercitam", e nenhum teste passava de 8; (5) **§V3 e §B21 deste arquivo** afirmavam verificação "em 1, 2, 4, 8 e 16 threads", e VS19 iterava `for n in 2 4 8`. Sondadas: 12 de 12 combinações de semente idênticas, `--scan reverse` idêntico em 1/2/4/8, valgrind com 0 erros e 0 vazamentos nas três variantes, 20×20 com 16/32/64 threads idêntico ao serial, `demo200` com 16 e 32 idêntico. | **O achado é que não houve achado, e é isso que vale ao texto.** A auditoria não encontrou defeito: encontrou **afirmação verdadeira e não verificada** — e do lado de fora ela é **indistinguível** de afirmação falsa não verificada. A diferença só aparece quando alguém mede, e o custo de descobrir é assimétrico: um estágio de horas agora, contra descobrir pelo texto depois de publicado. Sexta ocorrência do padrão da armadilha 4 do HANDOFF, em cara nova — não é critério errado (§B14), nem medição isolada virando achado (§B25), nem "o código calcula e joga fora" (§B8, §B15, §B16, §B17): é **cobertura afirmada e não conferida**. E se distingue de todos os anteriores num ponto: os itens 4 e 5 não são defeitos herdados do SLEUTH original, são **nossos**, escritos neste trabalho por quem já conhecia o padrão — o 4 num comentário, o 5 **num invariante deste arquivo**. Virou VS28–VS32; detalhe em §D1, §D2 e §B1 de `specs/S07-rede-de-verificacao.md`. |
| B29 | 20/09 | **§B26 reproduz em outra sessão, e a linha de 1 thread é a régua do ruído que faltava.** T0 do S07 rodou `harness/probe-s06.sh` **versionada e não modificada** (`GRADES=ladrilhado REP=8`), em sessão diferente da que produziu §B26. Fase 4 a 8 threads, em ms: A 35,48 → **35,40**, B 15,48 → **15,73**, C 15,25 → **15,55**, piso 15,40 → **15,52** — **as quatro dentro de 2 %**, num experimento cujo ruído declarado é de 10 a 20 %. O custo sobre o piso sai +128 % / +1 % / +0 %, contra +130 % / +1 % / −1 %. As duas guardas também reproduzem: a curva de granularidade de B (1/8/64/1024/16384 travas) dá +150 %/+23 %/+4 %/+1 %/+2 % contra +146 %/+19 %/+6 %/+1 %/+3 %, e a razão de travas por célula entre a grade ladrilhada e a artificial dá **326×** contra 325×. **Mas o ano inteiro não reproduz:** B e C aparecem 3 % e 7 % **abaixo do piso**, o que é fisicamente impossível — o piso é o mesmo binário sem exclusão nenhuma. A causa está visível na própria tabela: na linha de 1 thread as quatro variantes degeneram no mesmo código serial (VS27) e deveriam dar quatro valores iguais; deram 180,08 / 187,08 / 179,89 / 178,89, **4,6 % de espalhamento em código byte a byte idêntico**. | **Paga a dívida que §B25 deixou em aberto e libera as tabelas de §B26 para o texto** — era a última pendência de medição da seção 4. §B26 é o segundo achado do projeto a passar no teste de remedição, depois de §B22, e agora passou nas quatro colunas e não só na de A. **E acrescenta o terceiro item da tríade metodológica:** §D3 do S06 diz que a **entrada** decide qual gargalo o experimento enxerga; §B27 diz que a **ordem** decide o sinal; §B29 diz que a **granularidade da medida** decide se há sinal — a mesma execução reproduz a etapa dentro de 2 % e não reproduz o total. Note que a sonda **já rotaciona a ordem** (§B27) e ainda assim o total oscila: rotacionar protege contra viés de posição, não contra variância. Regra prática para o S08: **a linha de 1 thread de qualquer tabela de variantes é a régua do ruído daquela execução e deve ser lida antes de qualquer número ser interpretado** — ela é código idêntico medido quatro vezes, então toda diferença ali é ruído por construção. Detalhe em §B2 e §B3 de `specs/S07-rede-de-verificacao.md`. |

| B30 | 20/09 | **A máquina tem 6 núcleos físicos, não 12 — e a matriz {1,2,4,8} de S08 não cabe neles.** `lscpu`: AMD Ryzen 5 4600G, 6 núcleos, 2 threads por núcleo, SMT ligado, um soquete, um nó NUMA, **L3 de 4 MiB compartilhado entre os seis**. Os 12 lógicos pareiam `0/1`, `2/3`, … `10/11`. O §T e o bloco de ambiente deste arquivo diziam "12 núcleos" e que eles "cobrem **confortavelmente** a matriz {1,2,4,8} threads de S08" — a 8 threads seis workers pegam núcleo inteiro e dois dividem com um irmão SMT. **E a pergunta foi feita e nunca respondida:** `history/2026-09-18-sessao-02-...md:528-530` registra, na sessão 02, "a de desenvolvimento tem 12 núcleos; **falta saber se é a definitiva e se SMT está ligado**". | **Sétima ocorrência do padrão da armadilha 4 do HANDOFF, e a mais pura de todas: uma questão explicitamente registrada como aberta virou premissa assentada sem ninguém decidir nada.** Não é afirmação não verificada como §B28 — é afirmação que o próprio projeto marcou como *pendente de verificação* e depois usou como se resolvida, em dois documentos duráveis. **Consequência técnica, e ela é um confundidor, não uma correção:** as etapas são separadas por barreira, então o worker mais lento dita o ritmo de todos; a 8 threads dois workers rodam degradados por SMT e degradam a fase inteira. Isso **não invalida** a comparação A vs B vs C de §B26 — o efeito atinge as três igualmente —, mas contamina a afirmação de **escalada** ("1,70× sob A contra 2,27×/2,29× sob B/C"), que mistura a trava saturando com os núcleos físicos acabando. **As duas causas não são separáveis com a escada atual**, que atravessa 6 sem medi-lo. O que as separa é o **ponto de 6 threads**, e ele entra na T4 do S08 por isso. Até lá, não escrever no texto nenhuma atribuição de causa para o joelho entre 4 e 8 threads. |

**Sobre B14–B16.** Os três nasceram em `specs/S03-harness.md` e foram
promovidos aqui em 18/09 porque um chat novo precisa vê-los **sem abrir o spec
do estágio**: o primeiro impede o original de rodar, e os outros dois são as
duas armadilhas de mapeamento que qualquer comparação contra o `avg.log` — em
S07 e S08, não só em S03 — tem de conhecer antes de desconfiar do próprio
código. Os demais achados do estágio continuam lá, que é onde pertencem.

---

## §R regras de trabalho

1. **Um estágio por vez.** O spec de um estágio só é escrito quando o anterior
   termina, porque os resultados do anterior mudam o conteúdo do seguinte
   (ver `specs/README.md` para a justificativa).
2. **`SLEUTH/` é somente-leitura.** É a referência e o baseline. Todo código novo
   vai para `sleuth-par/`.
3. Toda decisão técnica que contrarie o plano original vira uma linha em §B
   deste arquivo, com data e consequência. O plano não é editado.
4. Todo bug encontrado durante a execução entra em §B do spec do estágio
   corrente; se gerar invariante nova, ela sobe para §V daqui.
5. O texto do TCC anda em paralelo ao código, não depois. Cada estágio de código
   alimenta uma seção do texto — a coluna correspondente está em cada spec.
6. Se o prazo apertar, a ordem de corte é: variantes B e C (viram trabalho
   futuro) → grades maiores que 1000×1000 → determinismo bit a bit (cai para
   equivalência estatística, declarada).
