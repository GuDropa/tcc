# Paralelização do núcleo do SLEUTH com threads POSIX

Código do TCC de Gustavo Dropa. Aqui estão o protótipo, a suíte de verificação,
as sondas de medição e as saídas cruas das medições. O texto do TCC é entregue
à parte.

## O que é

O SLEUTH é um autômato celular de crescimento urbano (Clarke, Hoppen e Gaydos,
1997). O trabalho parte de `spread-simplified.c`, o recorte do núcleo do modelo
enviado pelo orientador, e o leva a um protótipo em C11 que executa o núcleo em
N threads POSIX. A exclusão mútua tem três variantes, e nas três a saída é
idêntica, bit a bit, à da execução serial.

## Estrutura

| caminho | conteúdo |
|---|---|
| `sleuth-par/` | o protótipo: código, `Makefile`, testes e harness |
| `sleuth-par/par.h` | o desenho do paralelismo, em cerca de 150 linhas quase todas de comentário. É o melhor ponto de partida para ler o código |
| `sleuth-par/tests/` | `run_checks.sh`, a suíte de verificação (VS1–VS32), e as saídas de referência em `golden/` |
| `sleuth-par/harness/` | construção do SLEUTH original como referência, conversor de dados e sondas de medição (`probe-*.sh`) |
| `sleuth-par/harness/out/` | saídas cruas das medições do S08, com o censo do ambiente em cada arquivo |
| `sleuth-par/data/demo200/` | a grade de entrada, 200×200, convertida dos GIFs do SLEUTH (ver `PROCEDENCIA.md`) |
| `SLEUTH/` | o SLEUTH 3.0 beta original, **sem modificação**. É a referência de corretude e o baseline de comparação. Foram omitidos só os binários pré-compilados do pacote (um ELF MIPS de 2001 e executáveis Windows), que não rodam aqui |
| `spread-simplified.c` | o ponto de partida enviado pelo orientador; `sleuth-par/spread.c` deriva dele |
| `SPEC.md` | spec mestre: objetivo, restrições, invariantes (§V) e o registro de achados e correções (§B) |
| `specs/` | um spec por estágio (S02–S08), com as decisões, as verificações e o que cada estágio encontrou |
| `plano-tcc-sleuth-paralelo.md` | o plano original. O §B do `SPEC.md` registra o que foi corrigido nele |

## Requisitos

Linux x86-64, gcc com suporte a C11, `-pthread` e ThreadSanitizer, GNU make,
valgrind e python3, além de `awk`, `cmp`, `diff` e `sha256sum`. Não há outras
dependências. Desenvolvido e testado em WSL com Ubuntu 24.04, gcc 13.3, GNU make
4.3 e valgrind 3.22.

## Compilar e verificar

```bash
cd sleuth-par
make                      # -std=c11 -Wall -Wextra -O2, asserts ligados
./tests/run_checks.sh     # a suíte inteira
```

A suíte termina com

```
== tudo passou, com 2 verificacao(oes) adiada(s) para o S08 ==
```

As duas adiadas (VS12 e VS13) são esperadas: dependem de tarefas do estágio de
desempenho, que está em andamento. A suíte cobre, entre outras coisas:

- o protótipo serial reproduz o SLEUTH original, compilado a partir de `SLEUTH/`
  numa cópia descartável em `harness/build/` (V7);
- 1, 2, 4, 8 e mais threads dão a mesma saída, byte a byte (V3);
- a ordem de varredura não altera o resultado (V4);
- as três variantes de exclusão mútua dão a mesma saída (V6);
- ThreadSanitizer e valgrind limpos com 8 threads, nas três variantes.

## Rodar

```bash
cd sleuth-par
ARGS="--load data/demo200 --years 20 --seed 1 --coeff 2,5,20,51,5 --critical-slope 15.0"

./sleuth-par $ARGS                           # serial
./sleuth-par $ARGS --threads 8 --variant A   # mutex global
./sleuth-par $ARGS --threads 8 --variant B   # 1024 mutexes, pela célula alvo
./sleuth-par $ARGS --threads 8 --variant C   # CAS atômico
./sleuth-par $ARGS --threads 8 --variant none  # sem proteção: corrida de dados
                                               # proposital, só para demonstração
./sleuth-par $ARGS --rng legacy              # gerador original do SLEUTH
```

O cabeçalho da saída informa o número de threads, a variante e o gerador.

Para a grade grande, 2000×2000, com a mesma fração urbana do `demo200`:

```bash
./harness/mktiled.sh 10      # gera data/demo2000 (33 MB, fora do versionamento)
./sleuth-par --load data/demo2000 --years 5 --seed 1 \
             --coeff 2,5,20,51,5 --critical-slope 15.0 --threads 8
```

## Medições

As tabelas estão nos specs, no §B de cada estágio. As saídas cruas do S08
estão em `sleuth-par/harness/out/`, e cada uma registra o ambiente antes e
depois da medida. Máquina: AMD Ryzen 5 4600G, 6 núcleos físicos com SMT (12
lógicos), WSL com Ubuntu 24.04.

## Estado

Estágios S02 a S07 fechados. O S08, de desempenho, está em andamento.

## Sobre os documentos

`SPEC.md` e `specs/` são o registro de trabalho do projeto: o que se decidiu,
o que se mediu e o que se descobriu errado depois. Em alguns trechos eles citam
`HANDOFF.md` e `history/`, que eram notas de passagem entre sessões de trabalho
com o assistente de IA (ver abaixo) e ficaram fora desta entrega.

## Uso de inteligência artificial

O projeto foi desenvolvido com o **Claude Code**, assistente de programação da
Anthropic baseado nos modelos Claude. Ele roda no terminal, lê e escreve os
arquivos do projeto e executa comandos.

**Onde foi usado.** Em todo o conteúdo deste repositório: a escrita e a
modificação do código de `sleuth-par/`, da suíte de verificação e dos scripts de
`harness/`; a execução das medições; e a redação do `SPEC.md`, dos specs em
`specs/` e deste README. No repositório de trabalho original, os 46 commits,
feitos de 18 a 21/09/2026, levam a linha `Co-Authored-By: Claude`. Este
repositório de entrega foi montado a partir daquele, sem o histórico.

**O que não veio do assistente.** O ponto de partida (`spread-simplified.c`), a
linguagem C, as matrizes globais e a estrutura do texto foram definidos pelo
orientador (§C do `SPEC.md`). O autor conduziu o trabalho: definiu o que cada
sessão atacava, decidiu entre as alternativas apresentadas e responde pelo
resultado.

**Como conferir.** A correção não depende de confiar no assistente.
`./tests/run_checks.sh` refaz a suíte VS1–VS32, e toda medição sai de uma sonda
versionada em `harness/`, que pode ser rodada de novo. Os próprios specs
registram afirmações que não se sustentaram quando foram medidas de novo, como a
§B23, retirada pela §B25 do `SPEC.md`.

## Créditos e referências

- SLEUTH 3.0 beta, do Project Gigalopolis, distribuído como
  `SLEUTH3.0beta_p01_linux`.
- CLARKE, K. C.; HOPPEN, S.; GAYDOS, L. A self-modifying cellular automaton
  model of historical urbanization in the San Francisco Bay area. *Environment
  and Planning B: Planning and Design*, v. 24, n. 2, p. 247–261, 1997.
- Jogo da Vida (seção 3.0 do texto): <https://github.com/GuDropa/game_of_life>,
  commit `b9ae65b`.
