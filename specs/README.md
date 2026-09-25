# specs/ — specs por estágio

O spec mestre é `../SPEC.md`. Ele é estável: objetivo, restrições, invariantes
globais (§V), a lista de estágios (§T) e os achados que corrigem o plano
original (§B).

Esta pasta guarda um arquivo por estágio. Cada um é um contrato pequeno e
executável: o que entra, o que sai, o que precisa ser verdade no fim.

## Convenção de nomes

```
S<NN>-<slug>.md
```

`NN` segue a numeração de §T do spec mestre e nunca é reusado. Se um estágio for
abandonado, o arquivo fica, com status `abandonado` e o motivo no topo — o
histórico de decisões é parte do TCC.

## Por que escrever um de cada vez

Os estágios têm dependência de **resultado**, não só de ordem:

- Se S02 revelar que os stubs não fecham e for preciso compilar contra a árvore
  original inteira, S03 e S05 mudam de forma.
- Se S04 não conseguir ancorar o RNG à célula, S07 deixa de testar igualdade bit
  a bit e passa a testar equivalência estatística — o que reescreve S07, S08 e a
  seção 4 do texto.
- Se S08 mostrar que a variante A já satura, o recorte de S06 muda.

Escrever os oito specs agora significaria reescrever metade depois de S02. O que
está congelado desde já é a **ordem** e os **invariantes** — e esses moram no
spec mestre, onde um chat novo os encontra sem depender de nenhum estágio.

## Ciclo de cada estágio

1. Escrever `S<NN>-*.md` (skill `/spec`), com §T próprio de tarefas.
2. Executar (skill `/build`, apontando para o spec do estágio).
3. Verificar contra os §V citados.
4. Marcar o estágio em §T do mestre; subir achados novos para §B do mestre.
5. Escrever a seção de texto que o estágio alimenta.
6. Só então escrever o spec seguinte.

## Estado

A tabela autoritativa de status é §T em `../SPEC.md`. Não duplicar aqui.

| arquivo | estágio |
|---|---|
| `../game_of_life/SPEC.md` | S01 — Jogo da Vida (pré-existente, fora desta pasta) |
| `S02-base-serial.md` | S02 — base serial portável |
| `S03-harness.md` | S03 — harness de teste |
| `S04-rng.md` | S04 — RNG ancorado à célula |
| `S05-varreduras.md` | S05 — paralelização das varreduras `O(N)` |
| `S06-regiao-critica.md` | S06 — variantes B e C da região crítica |
| `S07-rede-de-verificacao.md` | S07 — as cinco propriedades afirmadas e não medidas |
| `S08-desempenho.md` | S08 — desempenho: o instrumento antes da tabela |

Os demais são escritos sob demanda.

## O ciclo funcionando: três exemplos

A regra "um de cada vez" já se pagou duas vezes, e das duas o motivo foi o
mesmo — **sondar antes de catalogar**, e deixar a sonda mudar o spec:

- **S04** foi escrito depois que `harness/probe-rng.sh` mediu o consumo de
  sorteios por fase. O número (99 % na fase 4) e o mecanismo (0,134 sorteio
  por célula varrida, porque o teste é curto-circuitado) reescreveram o §D do
  spec inteiro. Sem a sonda, V4 teria sido escrita como "ancorado à célula" —
  o que não se aplica a duas das três fases.
- **S05** teve o recorte **ampliado** por `harness/probe-s05.sh`: o §T do
  mestre dizia "paralelização de `spr_phase4`", e a medição mostrou que a
  fase 4 é 51,7 % do relógio, com outras três varreduras `O(N)` ao lado. O
  recorte original teria teto de 2,07×. Ver §B19 do mestre.

- **S06** teve a **ordem dos candidatos invertida** por `harness/probe-s06.sh`.
  O §T do mestre elegia o pool persistente como "o maior ganho disponível" e
  mandava medir as variantes B e C antes de implementá-las, porque o ganho
  delas "pode ser pequeno". Medido: a trava custa **+126 %** na fase 4 e o pool
  custa 6 %. E a sondagem descobriu *por que* ninguém tinha visto isso — as
  entradas do projeto não continham o gargalo (§D3 do spec).

Nos três casos o spec escrito seis dias antes estaria errado — não por
descuido, mas porque a informação que o corrige só existe depois do estágio
anterior.

O S06 acrescenta um quarto caso, mais desconfortável: a sondagem **derrubou um
achado já documentado** do estágio anterior (§B23, remedido e não reproduzido —
§B25 do mestre). Sondar antes de catalogar também serve para recatalogar.

E o **S08** acrescenta o quinto, que é o mais barato de todos: a sondagem
(`harness/probe-s08-ruido.sh`) não mudou o escopo do estágio — mudou uma
**verificação já escrita**. VS12 estava redigida desde o S03 como "`--repeat 5`
reporta mediana", parada cinco estágios à espera deste, e a medição mostrou que
a mediana é o estimador com o pior "pior caso" dos três (23,8 % de espalhamento
entre grupos, contra 4,4 % do mínimo). Implementá-la como estava teria posto o
critério errado **dentro da suíte**, que é o lugar mais caro de corrigir. Ver
§D2 e §D4 do spec.

O padrão que os cinco casos desenham: o custo de descobrir cresce com a
distância até a execução. S04 e S05 corrigiram escopo antes de existir código;
o S06 corrigiu um achado antes de ir para o texto; o S08 corrigiu um critério
antes de virar teste.
