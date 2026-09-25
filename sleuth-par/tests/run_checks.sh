#!/usr/bin/env bash
#
# run_checks.sh — verificacao dos estagios S02 a S07.
#
# Uma checagem por item do §V de specs/S02-base-serial.md (VS1-VS7), de
# specs/S03-harness.md (VS8-VS14), de specs/S04-rng.md (VS15-VS18), de
# specs/S05-varreduras.md (VS19-VS23), de specs/S06-regiao-critica.md
# (VS24-VS27) e de specs/S07-rede-de-verificacao.md (VS28-VS32). Roda a
# partir de qualquer diretorio.
#
#   ./tests/run_checks.sh
#
# Sai != 0 se alguma checagem falhar. Checagens ADIADAS (VS12, VS13) nao
# derrubam a suite, mas aparecem no relatorio: o §V12 do mestre manda nao
# esconder desvio, e uma verificacao que some do relatorio e uma verificacao
# que ninguem lembra de retomar.
#
set -u

cd "$(dirname "$0")/.." || exit 1

BIN=./sleuth-par
HARNESS=harness
DATA=./data/demo200
GIFS=../SLEUTH/Input/demo200
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fails=0
skips=0
pass () { printf '  ok    %s\n' "$1"; }
fail () { printf '  FALHA %s\n' "$1"; fails=$((fails + 1)); }
skip () { printf '  --    %s\n' "$1"; skips=$((skips + 1)); }

echo "== S02: verificacao =="

# ---------------------------------------------------------------- VS1 -------
# build limpo, zero warnings em -Wall -Wextra
echo
echo "VS1  build sem warnings"
make clean >/dev/null 2>&1
build_log=$(make 2>&1)
if [ $? -ne 0 ]; then
  fail "a compilacao falhou"
  echo "$build_log" | tail -20
elif echo "$build_log" | grep -q "warning:"; then
  fail "ha warnings"
  echo "$build_log" | grep "warning:" | head -10
else
  pass "compila limpo"
fi

# ---------------------------------------------------------------- VS2 -------
# 5 anos em 20x20 sob valgrind, sem erro e sem vazamento
echo
echo "VS2  valgrind limpo"
if ! command -v valgrind >/dev/null 2>&1; then
  fail "valgrind nao instalado"
else
  if valgrind --error-exitcode=1 --leak-check=full --errors-for-leak-kinds=all \
       "$BIN" --rows 20 --cols 20 --years 5 --seed 1 \
       >/dev/null 2>"$TMP/vg.txt"; then
    pass "0 erros, 0 vazamentos"
  else
    fail "valgrind acusou"
    grep -E "ERROR SUMMARY|lost" "$TMP/vg.txt" | head -5
  fi
fi

# ---------------------------------------------------------------- VS3 -------
# z so e escrito no merge, dentro de spr_spread (invariante V2)
echo
echo "VS3  z somente-leitura fora do merge"
# procura atribuicao a z[...] ou g_z[...] em spread.c
z_writes=$(grep -nE '(^|[^_a-zA-Z])(g_)?z\[[^]]*\][[:space:]]*=[^=]' spread.c || true)
n_writes=$(printf '%s' "$z_writes" | grep -c . )
if [ "$n_writes" -eq 1 ] && printf '%s' "$z_writes" | grep -q 'z\[i\] = delta\[i\]'; then
  pass "unica escrita e 'z[i] = delta[i]' no merge"
else
  fail "esperava 1 escrita em z (o merge), achei $n_writes"
  printf '%s\n' "$z_writes"
fi

# ---------------------------------------------------------------- VS4 -------
# nenhuma alocacao fora da inicializacao (invariante V9)
echo
echo "VS4  sem alocacao no caminho de execucao"
allocs=$(grep -nE '\b(malloc|calloc|realloc|strdup)\b' \
                 spread.c util.c stubs.c main.c load.c || true)
if [ -z "$allocs" ]; then
  pass "alocacao so em grid.c"
else
  fail "alocacao fora de grid.c"
  printf '%s\n' "$allocs"
fi

# ---------------------------------------------------------------- VS5 -------
# OFFSET nao chama funcao (invariante V14)
echo
echo "VS5  OFFSET sem chamada de funcao"
offset_def=$(grep -E '^#define OFFSET' globals.h)
if printf '%s' "$offset_def" | grep -qE '\bigrid_Get|\(\)'; then
  fail "OFFSET ainda chama funcao: $offset_def"
else
  pass "$offset_def"
fi

# ---------------------------------------------------------------- VS6 -------
# mesma semente, mesma saida
echo
echo "VS6  determinismo entre execucoes"
"$BIN" --years 8 --seed 42 --dump "$TMP/a.txt" >"$TMP/a.out" 2>&1
"$BIN" --years 8 --seed 42 --dump "$TMP/b.txt" >"$TMP/b.out" 2>&1
"$BIN" --years 8 --seed 43 --dump "$TMP/c.txt" >/dev/null 2>&1
if cmp -s "$TMP/a.txt" "$TMP/b.txt" && cmp -s "$TMP/a.out" "$TMP/b.out"; then
  if cmp -s "$TMP/a.txt" "$TMP/c.txt"; then
    fail "semente diferente produziu saida igual — semente ignorada?"
  else
    pass "semente 42 reproduz; semente 43 diverge"
  fi
else
  fail "duas execucoes com a mesma semente divergiram"
fi

# ---------------------------------------------------------------- B7 --------
# o bug de aliasing do workspace e real e a correcao muda o resultado
#
# growth_col comeca em `nrows` no original e em `total_pixels` aqui. A
# corrupcao so aparece quando growth_count > nrows, entao o gatilho e uma
# grade com muitos pixels e poucas linhas.
echo
echo "B7   bug de aliasing do workspace"
make clean >/dev/null 2>&1
make >/dev/null 2>&1
"$BIN" --rows 8 --cols 600 --years 6 --seed 7 --dump "$TMP/fixed.txt" >/dev/null 2>&1
make clean >/dev/null 2>&1
make CFLAGS="-std=c11 -Wall -Wextra -O2 -g -pthread -UNDEBUG -DSLEUTH_ORIGINAL_B7_BUG" \
     >/dev/null 2>&1
"$BIN" --rows 8 --cols 600 --years 6 --seed 7 --dump "$TMP/buggy.txt" >/dev/null 2>&1
if cmp -s "$TMP/fixed.txt" "$TMP/buggy.txt"; then
  fail "corrigido e original deram a mesma saida — o bug nao foi exercitado"
else
  pass "corrigido difere do original: aliasing confirmado"
fi
make clean >/dev/null 2>&1
make >/dev/null 2>&1

# ---------------------------------------------------------------- VS7 -------
# o SLEUTH original compila e roda — baseline de V7
#
# MUDOU EM S03 (T12). Antes esta checagem copiava ../SLEUTH para um mktemp,
# compilava do zero (~30 s) e apagava no trap EXIT: provava que o original
# compila e nao deixava nada com que comparar (§B6 do spec do estagio).
#
# Agora confere o baseline DURAVEL de harness/build/sleuth-ref, que e o
# binario que o compare_v7.sh de fato usa. E alvo de make com dependencia no
# patch, entao em rodada repetida custa ~0 s em vez de 30 s.
#
# Conferir que e ELF nativo nao e paranoia: o `grow` que vem no tarball e um
# ELF MIPS N32 de 2001, e um teste de `[ -x ]` sozinho aceitaria ele.
echo
echo "VS7  baseline de referencia construido e nativo"
if ! make -C "$HARNESS" baseline >"$TMP/baseline.log" 2>&1; then
  fail "a construcao do grow de referencia falhou"
  tail -15 "$TMP/baseline.log"
else
  grow=$HARNESS/build/sleuth-ref/grow
  if [ ! -x "$grow" ]; then
    fail "nao achei $grow executavel"
  elif ! file "$grow" | grep -q "x86-64"; then
    fail "o grow nao e ELF x86-64: $(file -b "$grow" | cut -c1-60)"
  else
    pass "grow nativo e duravel ($(stat -c%s "$grow") bytes)"
  fi
fi

# ============================================================================
echo
echo "== S03: verificacao =="

# ---------------------------------------------------------------- VS8 -------
# o gif2asc reproduz exatamente as contagens de §D1 do spec do estagio.
#
# Se isto quebrar, ou o gd.c mudou de comportamento ou as grades de entrada
# nao sao as que o spec descreve — e nesse caso TUDO abaixo perde sentido,
# porque os dados do demo200 sao derivados daqui.
echo
echo "VS8  gif2asc reproduz os histogramas de §D1"
if ! make -C "$HARNESS" gif2asc >"$TMP/gif2asc.log" 2>&1; then
  fail "a compilacao do gif2asc falhou"
  tail -10 "$TMP/gif2asc.log"
else
  g2a=$HARNESS/build/gif2asc
  vs8=0

  # urbanas e estradas: binarias 0/255, o que importa e a contagem de nao-zero
  for par in urban.1930:598 urban.1950:861 urban.1970:1821 urban.1990:3881 \
             roads.1930:375 roads.1950:665 roads.1970:945 roads.1990:1104; do
    nome=${par%:*}
    esperado=${par#*:}
    obtido=$("$g2a" "$GIFS/demo200.$nome.gif" --stats 2>/dev/null \
             | sed 's/.*nao-zero=//' | awk '{print $1}')
    if [ "$obtido" != "$esperado" ]; then
      fail "$nome: nao-zero=$obtido, §D1 diz $esperado"
      vs8=1
    fi
  done

  # slope e excluded nao sao binarias: comparar o histograma e mais forte que
  # a contagem de nao-zero, e e o que §D1 tabela.
  slope_max=$("$g2a" "$GIFS/demo200.slope.gif" --stats 2>/dev/null \
              | sed 's/.*max=//' | awk '{print $1}')
  slope_zero=$("$g2a" "$GIFS/demo200.slope.gif" --stats 2>/dev/null \
               | grep -o '{0:[0-9]*' | cut -d: -f2)
  if [ "$slope_max" != "41" ] || [ "$slope_zero" != "10722" ]; then
    fail "slope: max=$slope_max zeros=$slope_zero, §D1 diz max=41 zeros=10722"
    vs8=1
  fi

  if ! "$g2a" "$GIFS/demo200.excluded.gif" --stats 2>/dev/null \
       | grep -q '{1:35775 100:4225 }'; then
    fail "excluded: histograma != {1:35775 100:4225}, o que §D1 tabela"
    vs8=1
  fi

  [ "$vs8" -eq 0 ] && \
    pass "10 grades conferem com §D1 (urbanas, estradas, slope, excluded)"
fi

# ----------------------------------------------------- VS9 / VS10 / VS11 ----
#
# As tres compartilham UMA execucao do grow de referencia: rodar o original
# 1990->2010 tres vezes seria desperdicio, e as tres perguntas se respondem
# olhando o mesmo avg.log. O compare_v7.sh --keep deixa os intermediarios.
echo
echo "VS9  o grow de referencia roda 1990->2010 e escreve avg.log"
v7_rc=0
./tests/compare_v7.sh --keep >"$TMP/v7.out" 2>&1 || v7_rc=$?

avg=$HARNESS/build/refout/avg.log
if [ ! -r "$avg" ]; then
  fail "o grow nao escreveu $avg"
  tail -20 "$TMP/v7.out"
else
  nanos=$(awk '$1 ~ /^[0-9]+$/' "$avg" | wc -l)
  if [ "$nanos" -ne 20 ]; then
    fail "o avg.log tem $nanos linhas de dados, esperado 20"
  else
    pass "exit 0 e 20 linhas de dados no avg.log"
  fi
fi

# ---------------------------------------------------------------- VS10 ------
# automodificacao neutralizada (§D4): os cinco coeficientes ficam constantes.
# Se variarem, o CRITICAL_LOW/CRITICAL_HIGH do cenario parou de funcionar e a
# comparacao de V7 estaria medindo outro modelo.
echo
echo "VS10 coeficientes constantes nos 20 anos (automodificacao neutralizada)"
if [ ! -r "$avg" ]; then
  fail "sem avg.log, VS10 nao pode ser avaliada"
else
  # %g normaliza o formato antes de comparar: o avg.log publica os
  # coeficientes com duas casas ("2.00 20.00 5.00 51.00 5.00") e §D4 os cita
  # como inteiros. Comparar a string crua contra a forma abreviada do spec ja
  # produziu uma falha falsa aqui — e a mesma armadilha de §B14, em miniatura:
  # a comparacao se faz no formato que a saida tem, nao no que o texto usa.
  valores=$(awk '$1 ~ /^[0-9]+$/ { printf "%g %g %g %g %g\n", $18, $19, $20, $21, $22 }' \
                "$avg" | sort -u)
  distintos=$(printf '%s\n' "$valores" | wc -l)
  if [ "$distintos" -ne 1 ]; then
    fail "os coeficientes variaram ($distintos combinacoes distintas)"
    awk '$1 ~ /^[0-9]+$/ { print "        ", $2, $18, $19, $20, $21, $22 }' "$avg"
  elif [ "$valores" != "2 20 5 51 5" ]; then
    fail "coeficientes constantes mas em '$valores', esperado '2 20 5 51 5'"
  else
    pass "diffus spread breed slp_res rd_grav = 2 20 5 51 5 nos 20 anos"
  fi
fi

# ---------------------------------------------------------------- VS11 ------
# V7 propriamente dita. O trabalho esta no compare_v7.sh; aqui so se propaga o
# veredito, porque ele ja foi executado acima.
echo
echo "VS11 V7: o prototipo serial reproduz o SLEUTH original"
if [ "$v7_rc" -eq 0 ]; then
  grep -E '^  ok ' "$TMP/v7.out"
else
  fail "compare_v7.sh saiu com codigo $v7_rc"
  sed 's/^/        /' "$TMP/v7.out" | tail -25
fi

# --------------------------------------------------------- VS12 / VS13 ------
#
# ADIADAS, com o estagio fechado como `~`. Dependem de T8 (`--repeat`, para
# cronometrar pela mediana) e de T7 (`testgen` configuravel), que foram
# movidas para o S08 — que e quem precisa delas; nada em S04-S07 depende.
# Ver §T de specs/S03-harness.md.
#
# Ficam listadas de proposito: sao as duas unicas verificacoes do §V do
# estagio que nao rodam, e some-las do relatorio transformaria um desvio
# declarado num esquecimento.
echo
echo "VS12 cronometragem V11 exclui I/O"
skip "adiada para o S08 junto com T8 (--repeat e mediana nao existem ainda)"
echo
echo "VS13 fracao urbana do testgen estavel entre 200x200 e 2000x2000"
skip "adiada para o S08 junto com T7 (testgen configuravel nao existe ainda)"

# ---------------------------------------------------------------- VS14 ------
# Determinismo sob --load (extensao do VS6, que usa a entrada artificial) e a
# intercambialidade dump <-> entrada, que §B13 mostrou estar so na documentacao
# ate T4: o --dump nao escrevia o cabecalho `nrows ncols` e ninguem tinha
# exercitado o caminho, porque ate entao nao havia --load para consumir.
echo
echo "VS14 determinismo com --load e intercambialidade dump/entrada"
if [ ! -d "$DATA" ]; then
  fail "nao achei $DATA — rode 'make -C harness data'"
else
  C="--years 5 --coeff 2,5,20,51,5 --critical-slope 15.0"

  $BIN --load "$DATA" $C --seed 1 --dump "$TMP/L1.txt" >"$TMP/L1.out" 2>&1
  $BIN --load "$DATA" $C --seed 1 --dump "$TMP/L2.txt" >"$TMP/L2.out" 2>&1
  $BIN --load "$DATA" $C --seed 2 --dump "$TMP/L3.txt" >/dev/null 2>&1

  if ! cmp -s "$TMP/L1.txt" "$TMP/L2.txt" || ! cmp -s "$TMP/L1.out" "$TMP/L2.out"; then
    fail "duas execucoes com --load e semente 1 divergiram"
  elif cmp -s "$TMP/L1.txt" "$TMP/L3.txt"; then
    fail "semente diferente produziu o mesmo dump — semente ignorada sob --load?"
  else
    # o dump de z realimenta o --load de outra execucao (§B13)
    mkdir -p "$TMP/realim"
    cp "$TMP/L1.txt" "$TMP/realim/z.txt"
    for g in roads excld slp; do cp "$DATA/$g.txt" "$TMP/realim/"; done
    if $BIN --load "$TMP/realim" --years 1 --seed 1 \
            --coeff 2,5,20,51,5 --critical-slope 15.0 >"$TMP/realim.out" 2>&1; then
      pass "semente 1 reproduz, semente 2 diverge, e o dump realimenta o --load"
    else
      fail "o dump de z nao serviu de entrada para --load (§B13 regrediu)"
      tail -10 "$TMP/realim.out"
    fi
  fi
fi

# ============================================================================
echo
echo "== S04: verificacao =="

# ------------------------------------------------------------- VS15/VS16 ----
# O gerador ancorado em isolamento. O programa esta em tests/rng_test.c e
# imprime uma linha por sub-checagem; aqui so se relata o veredito, para nao
# afogar a suite.
echo
echo "VS15 rng_double e pura  ·  VS16 chaves vizinhas nao se correlacionam"
if ! make tests/rng_test >"$TMP/rngbuild.txt" 2>&1; then
  fail "tests/rng_test nao compilou"
  tail -10 "$TMP/rngbuild.txt"
elif ./tests/rng_test >"$TMP/rng.txt" 2>&1; then
  pass "$(grep -c '^  ok' "$TMP/rng.txt") sub-checagens: pureza, media, variancia, autocorrelacao, qui-quadrado, avalanche"
else
  fail "o gerador reprovou"
  grep 'FALHA' "$TMP/rng.txt" | head -5
fi

# ---------------------------------------------------------------- VS17 ------
# V4, e o teste que o S05 precisa que exista ANTES de haver qualquer thread.
#
# Verifica os DOIS modos de proposito. Que `anchored` nao mude com a inversao e
# a afirmacao; que `legacy` MUDE e a prova de que o teste tem dentes. Um VS17
# que passasse nos dois modos estaria medindo outra coisa — provavelmente que a
# fase 4 nao esta sendo exercitada.
echo
echo "VS17 V4: a ordem de varredura nao influi no modo anchored"
if [ ! -d "$DATA" ]; then
  fail "nao achei $DATA — rode 'make -C harness data'"
else
  V4="--load $DATA --years 20 --seed 1 --coeff 2,5,20,51,5 --critical-slope 15.0"

  $BIN $V4 --rng anchored --scan forward --dump "$TMP/a-fwd.txt" >/dev/null 2>&1
  $BIN $V4 --rng anchored --scan reverse --dump "$TMP/a-rev.txt" >/dev/null 2>&1
  $BIN $V4 --rng legacy   --scan forward --dump "$TMP/l-fwd.txt" >/dev/null 2>&1
  $BIN $V4 --rng legacy   --scan reverse --dump "$TMP/l-rev.txt" >/dev/null 2>&1

  if ! cmp -s "$TMP/a-fwd.txt" "$TMP/a-rev.txt"; then
    fail "anchored mudou ao inverter a varredura — V4 NAO vale, e S05 falharia"
  elif cmp -s "$TMP/l-fwd.txt" "$TMP/l-rev.txt"; then
    fail "legacy tambem nao mudou — o teste nao tem dentes, a fase 4 nao esta sendo exercitada"
  else
    pass "anchored identico ao inverter as linhas; legacy diverge, como tem de divergir"
  fi
fi

# ---------------------------------------------------------------- VS18 ------
# A rede de seguranca do estagio: separa "mudou porque troquei o gerador" de
# "mudou porque errei a propagacao da chave". O dump de referencia foi colhido
# ANTES de T3 e esta versionado.
echo
echo "VS18 --rng legacy reproduz bit a bit a saida de antes do S04"
GOLDEN=./tests/golden/z-pre-S04.txt
if [ ! -r "$GOLDEN" ]; then
  fail "nao achei $GOLDEN — a rede de seguranca do S04 sumiu do repositorio"
elif [ ! -d "$DATA" ]; then
  fail "nao achei $DATA — rode 'make -C harness data'"
else
  $BIN --load "$DATA" --years 20 --seed 1 --coeff 2,5,20,51,5 \
       --critical-slope 15.0 --rng legacy --dump "$TMP/vs18.txt" >/dev/null 2>&1
  if cmp -s "$TMP/vs18.txt" "$GOLDEN"; then
    pass "identico ao dump pre-S04: T3/T4/T5 nao mudaram comportamento por acidente"
  else
    fail "legacy divergiu do pre-S04 — a propagacao da chave alterou o modelo"
    cmp "$TMP/vs18.txt" "$GOLDEN" | head -3
  fi
fi

# ============================================================================
echo
echo "== S05: verificacao =="

REF="--load $DATA --years 20 --seed 1 --coeff 2,5,20,51,5 --critical-slope 15.0"

# ---------------------------------------------------------------- VS19 ------
# V3, a invariante central do trabalho: a saida com N threads e identica byte a
# byte a de 1 thread.
#
# O hash por ano localiza o PRIMEIRO ano divergente sem despejar a grade — e
# por isso que ele existe desde o S03. Uma divergencia no ano 1 e outra no ano
# 17 tem causas diferentes: a primeira e particionamento, a segunda costuma ser
# acumulador.
echo
echo "VS19 V3: --threads 1/2/4/8 dao o mesmo z, byte a byte"
if [ ! -d "$DATA" ]; then
  fail "nao achei $DATA — rode 'make -C harness data'"
else
  $BIN $REF --threads 1 --dump "$TMP/z1.txt" --hash >"$TMP/h1.txt" 2>&1
  vs19=0
  for n in 2 4 8; do
    $BIN $REF --threads "$n" --dump "$TMP/z$n.txt" --hash >"$TMP/h$n.txt" 2>&1
    if ! cmp -s "$TMP/z1.txt" "$TMP/z$n.txt"; then
      fail "--threads $n divergiu de --threads 1"
      # primeiro ano em que os hashes diferem
      ano=$(diff <(awk 'NF>3 {print $1, $NF}' "$TMP/h1.txt") \
                 <(awk 'NF>3 {print $1, $NF}' "$TMP/h$n.txt") \
            | awk '/^</ {print $2; exit}')
      printf '        primeiro ano divergente: %s\n' "${ano:-?}"
      vs19=1
    fi
  done
  [ "$vs19" -eq 0 ] && pass "1, 2, 4 e 8 threads produzem o mesmo z em 20 anos"
fi

# ---------------------------------------------------------------- VS20 ------
# As estatisticas, e nao so o z.
#
# ATENCAO AO QUE ESTA SENDO VERIFICADO, que mudou durante o estagio. A tabela
# anual — sng sdc og rt cresc pop cresc_decl decl_z — e identica para todo N, e
# e isso que "as sete estatisticas" quer dizer. Ja os CINCO contadores de
# tentativa de urbanizacao NAO sao todos invariantes, e isso e achado do
# estagio e nao defeito (§B21):
#
#   urban_success, z_failure          exatos para todo N
#   delta + slope + excluded          SOMA exata; a reparticao varia
#
# A razao esta em §B21: a exclusao mutua torna o MODELO independente da ordem,
# nao a INSTRUMENTACAO. Quem perde a corrida por um alvo e contado em `delta`
# se chegou depois do vencedor e em `slope`/`excluded` se chegou antes e
# reprovou por conta propria. Verificar "os cinco contadores identicos" seria
# cobrar do programa uma propriedade que ele nao tem — e a armadilha 4 do
# HANDOFF em forma nova: criterio errado disfarcado de bug.
echo
echo "VS20 estatisticas identicas entre contagens de threads"
if [ ! -d "$DATA" ]; then
  fail "nao achei $DATA — rode 'make -C harness data'"
else
  vs20=0
  # a tabela anual: da linha do cabecalho de colunas ate a linha em branco
  tabela () { awk '$1 ~ /^[0-9]+$/ && NF >= 9 { print }' "$1"; }
  for n in 2 4 8; do
    if ! cmp -s <(tabela "$TMP/h1.txt") <(tabela "$TMP/h$n.txt"); then
      fail "a tabela anual de --threads $n divergiu de --threads 1"
      diff <(tabela "$TMP/h1.txt") <(tabela "$TMP/h$n.txt") | head -4
      vs20=1
    fi
  done

  # os contadores: o que e exato, e o que so tem soma exata
  cnt () {
    awk '/^  sucesso/       { s=$2 }
         /^  ja era urbana/ { z=$5 }
         /^  ja crescida/   { d=$4 }
         /^  declividade/   { p=$2 }
         /^  area excluida/ { e=$3 }
         END { print s, z, d+p+e }' "$1"
  }
  base=$(cnt "$TMP/h1.txt")
  for n in 2 4 8; do
    got=$(cnt "$TMP/h$n.txt")
    if [ "$got" != "$base" ]; then
      fail "invariantes dos contadores quebraram em --threads $n"
      printf '        1 thread : sucesso/z_fail/(delta+slope+excl) = %s\n' "$base"
      printf '        %s threads: %s\n' "$n" "$got"
      vs20=1
    fi
  done
  [ "$vs20" -eq 0 ] && \
    pass "tabela anual identica; sucesso, z_fail e a soma das reprovas exatos (§B21)"
fi

# ---------------------------------------------------------------- VS21 ------
# A rede de seguranca do estagio, colhida ANTES de existir qualquer thread.
#
# Separa "mudou porque paralelizei" de "mudou porque errei ao reorganizar o
# codigo" — e este estagio reorganizou bastante: spr_spread virou disparo, as
# fases ganharam acumulador, as varreduras ganharam faixa.
#
# A PRIMEIRA LINHA E IGNORADA de proposito. O cabecalho passou a dizer a
# contagem de threads e a variante (T2, T6), entao ele MUDOU em relacao ao
# arquivo de referencia, que diz "serial". Comparar a partir da linha 2 e o
# certo; comparar tudo acusaria uma divergencia que e a tarefa T2 funcionando.
echo
echo "VS21 --threads 1 reproduz bit a bit a saida pre-S05"
G5=./tests/golden/z-pre-S05-anchored.txt
G4=./tests/golden/z-pre-S04.txt
GRUN=./tests/golden/run-pre-S05-anchored.txt
if [ ! -r "$G5" ] || [ ! -r "$G4" ] || [ ! -r "$GRUN" ]; then
  fail "a rede de seguranca do S05 sumiu de tests/golden/"
elif [ ! -d "$DATA" ]; then
  fail "nao achei $DATA — rode 'make -C harness data'"
else
  vs21=0
  cmp -s "$TMP/z1.txt" "$G5" || { fail "anchored divergiu do z pre-S05"; vs21=1; }

  $BIN $REF --threads 1 --rng legacy --dump "$TMP/zleg.txt" >/dev/null 2>&1
  cmp -s "$TMP/zleg.txt" "$G4" || { fail "legacy divergiu do z pre-S04"; vs21=1; }

  if ! cmp -s <(tail -n +2 "$GRUN") <(tail -n +2 "$TMP/h1.txt"); then
    fail "a saida textual divergiu de run-pre-S05 (fora o cabecalho)"
    diff <(tail -n +2 "$GRUN") <(tail -n +2 "$TMP/h1.txt") | head -6
    vs21=1
  fi
  [ "$vs21" -eq 0 ] && \
    pass "z e saida textual identicos ao pre-S05 nos dois modos de RNG"
fi

# ---------------------------------------------------------------- VS22 ------
# V7 continua de pe. O trabalho pesado ja foi feito em VS11, que roda o
# compare_v7.sh; aqui so se garante que passar --threads 1 EXPLICITO nao muda
# nada — o oraculo externo nao pode depender de uma flag ter sido omitida.
echo
echo "VS22 V7 sobrevive ao --threads explicito"
if [ "$v7_rc" -ne 0 ]; then
  fail "VS11 ja havia falhado; V7 nao esta de pe para ser reverificada"
elif [ ! -r "$G4" ]; then
  fail "nao achei $G4"
else
  $BIN $REF --threads 1 --rng legacy --dump "$TMP/v22.txt" >/dev/null 2>&1
  if cmp -s "$TMP/v22.txt" "$G4"; then
    pass "--threads 1 --rng legacy identico ao oraculo de V7"
  else
    fail "--threads 1 explicito alterou a saida legacy"
  fi
fi

echo
echo "== S06: verificacao =="

# ------------------------------------------------------------ VS24/VS25 -----
# As tres variantes da regiao critica (V6), agora que existem as tres.
#
# VS24 e VS19 com um eixo a mais: o que VS19 faz para a contagem de threads,
# VS24 faz para a variante. O ponto e que UMA VARIANTE MAIS RAPIDA QUE MUDE A
# SAIDA NAO E UMA OTIMIZACAO: as tres protegem o mesmo read-modify-write e tem
# de produzir o mesmo z, entre si e contra o serial.
#
# VS25 nao e repeticao de VS20, e a diferenca esta em §D5 do spec do estagio. A
# variante C muda a ORDEM EM QUE A DISPUTA E DESCOBERTA: sob A e B, quem perde
# e barrado na leitura de delta e nunca chega a avaliar a declividade; sob C,
# quem perde avalia a cascata inteira e so descobre a derrota no CAS. A soma
# das tres reprovas tem de continuar exata mesmo assim — e e por isso que ela e
# a coisa certa a cobrar. Cobrar os cinco contadores identicos reprovaria
# codigo correto, e sob C mais ainda que sob A (§B21 do mestre, armadilha 4 do
# HANDOFF).
echo
echo "VS24 V3 sob as variantes A, B e C  ·  VS25 as invariantes de §B21 nas tres"
if [ ! -d "$DATA" ]; then
  fail "nao achei $DATA — rode 'make -C harness data'"
else
  vs24=0; vs25=0
  # a tabela anual, isolada da saida textual (mesma forma usada por VS20)
  anual () { awk '$1 ~ /^[0-9]+$/ && NF >= 9 { print }' "$1"; }
  # o que §B21 diz ser invariante: sucesso, z_failure, e a SOMA das tres
  inv () {
    awk '/^  sucesso/       { s=$2 }
         /^  ja era urbana/ { z=$5 }
         /^  ja crescida/   { d=$4 }
         /^  declividade/   { p=$2 }
         /^  area excluida/ { e=$3 }
         END { print s, z, d+p+e }' "$1"
  }

  # o padrao de comparacao e o serial da variante A, que e o mesmo z que VS19
  # ja comparou contra o golden pre-S05 — entao esta cadeia chega ate la
  base_inv=$(inv "$TMP/h1.txt")

  for v in A B C; do
    for n in 1 2 4 8; do
      $BIN $REF --threads "$n" --variant "$v" \
           --dump "$TMP/z-$v-$n.txt" --hash >"$TMP/h-$v-$n.txt" 2>&1

      if ! cmp -s "$TMP/z1.txt" "$TMP/z-$v-$n.txt"; then
        fail "--variant $v --threads $n divergiu do z de referencia"
        vs24=1
      fi
      if ! cmp -s <(anual "$TMP/h1.txt") <(anual "$TMP/h-$v-$n.txt"); then
        fail "a tabela anual de --variant $v --threads $n divergiu"
        diff <(anual "$TMP/h1.txt") <(anual "$TMP/h-$v-$n.txt") | head -4
        vs25=1
      fi
      got=$(inv "$TMP/h-$v-$n.txt")
      if [ "$got" != "$base_inv" ]; then
        fail "invariantes de §B21 quebraram em --variant $v --threads $n"
        printf '        esperado: sucesso/z_fail/(delta+slope+excl) = %s\n' "$base_inv"
        printf '        obtido  : %s\n' "$got"
        vs25=1
      fi
    done
  done

  [ "$vs24" -eq 0 ] && pass "A, B e C dao o mesmo z em 1, 2, 4 e 8 threads (V6)"
  [ "$vs25" -eq 0 ] && \
    pass "tabela anual identica nas tres; a soma das reprovas exata tambem sob C"
fi

# ---------------------------------------------------------------- VS27 ------
# VS21 com o eixo de variante, e e a checagem que amarra a ponta serial de V6.
#
# "Selecionaveis em tempo de execucao pela mesma compilacao" so quer dizer
# alguma coisa se a selecao nao alterar o que nao devia: com uma thread nao ha
# com quem competir, entao as quatro variantes tem de degenerar no MESMO codigo
# serial e reproduzir bit a bit a saida colhida antes do S05 — inclusive
# `none`, que so e perigosa quando ha mais de uma thread.
#
# E ela cobre a T1 de passagem: a contagem de populacao mudou de dono neste
# estagio (saiu de util_count_pixels e entrou no laco do merge), e `pop` e uma
# das colunas comparadas aqui.
echo
echo "VS27 --threads 1 reproduz a saida pre-S05 em TODAS as variantes"
if [ ! -r "$GRUN" ] || [ ! -r "$G5" ]; then
  fail "a rede de seguranca do S05 sumiu de tests/golden/"
elif [ ! -d "$DATA" ]; then
  fail "nao achei $DATA — rode 'make -C harness data'"
else
  vs27=0
  for v in A B C none; do
    $BIN $REF --threads 1 --variant "$v" \
         --dump "$TMP/z27-$v.txt" --hash >"$TMP/h27-$v.txt" 2>/dev/null
    cmp -s "$TMP/z27-$v.txt" "$G5" || {
      fail "--threads 1 --variant $v divergiu do z pre-S05"; vs27=1; }
    if ! cmp -s <(tail -n +2 "$GRUN") <(tail -n +2 "$TMP/h27-$v.txt"); then
      fail "--threads 1 --variant $v mudou a saida textual (fora o cabecalho)"
      diff <(tail -n +2 "$GRUN") <(tail -n +2 "$TMP/h27-$v.txt") | head -4
      vs27=1
    fi
  done
  [ "$vs27" -eq 0 ] && \
    pass "as quatro variantes degeneram no mesmo serial, com a pop ja fundida"
fi

# ---------------------------------------------------------- VS23/VS26 ------
# ThreadSanitizer.
#
# V3 e TSan medem coisas DIFERENTES e as duas sao necessarias: V3 diz "o
# resultado e o mesmo", TSan diz "o programa e bem definido". Este estagio
# mediu o caso em que as duas discordam — com --variant none e 8 threads, 6 de
# 6 execucoes no demo200 deram o z certo, e o programa continuava com corrida
# de dados. E o cenario a temer, porque sobrevive a uma bateria de testes e
# falha num compilador diferente.
#
# Por isso a checagem tem DUAS metades, como VS17: as variantes tem de sair
# limpas e `none` tem de ACUSAR. Uma checagem que passasse nos dois lados nao
# estaria medindo nada — provavelmente o binario nao foi construido com o
# sanitizador.
#
# VS26 acrescenta B e C ao lado limpo, e sob C a checagem ganha um dente a
# mais: o TSan precisa RECONHECER as operacoes atomicas. Se ele acusar corrida
# sobre delta na variante C, ou o CAS nao esta atomico ou esta na largura
# errada — §B4 do mestre, o `unsigned char expected` do plano original, que
# compararia um byte de uma celula de oito e deixaria sete desprotegidos. E
# esse o modo de falha que esta linha existe para pegar.
echo
echo "VS23/VS26 ThreadSanitizer limpo em A, B e C, e acusando na 'none'"
if ! make tsan >"$TMP/tsanbuild.txt" 2>&1; then
  fail "make tsan nao compilou"
  tail -10 "$TMP/tsanbuild.txt"
else
  vs23=0
  corridas () { grep -c "WARNING: ThreadSanitizer" "$1" 2>/dev/null || true; }

  for v in A B C; do
    $BIN --rows 20 --cols 20 --years 5 --seed 1 --threads 4 --variant "$v" \
         >/dev/null 2>"$TMP/ts-$v-20.txt"
    $BIN --load "$DATA" --years 5 --seed 1 --coeff 2,5,20,51,5 \
         --critical-slope 15.0 --threads 4 --variant "$v" \
         >/dev/null 2>"$TMP/ts-$v-demo.txt"

    if [ "$(corridas "$TMP/ts-$v-20.txt")" -ne 0 ]; then
      fail "variante $v acusou corrida em 20x20"
      grep -A6 "WARNING: ThreadSanitizer" "$TMP/ts-$v-20.txt" | head -8
      vs23=1
    fi
    if [ "$(corridas "$TMP/ts-$v-demo.txt")" -ne 0 ]; then
      fail "variante $v acusou corrida no demo200"
      grep -A6 "WARNING: ThreadSanitizer" "$TMP/ts-$v-demo.txt" | head -8
      vs23=1
    fi
  done

  $BIN --load "$DATA" --years 5 --seed 1 --coeff 2,5,20,51,5 \
       --critical-slope 15.0 --threads 4 --variant none \
       >/dev/null 2>"$TMP/ts-none.txt"
  if [ "$(corridas "$TMP/ts-none.txt")" -eq 0 ]; then
    fail "a variante 'none' NAO acusou — o sanitizador nao esta tendo efeito"
    vs23=1
  fi

  [ "$vs23" -eq 0 ] && \
    pass "A, B e C limpas em 20x20 e demo200; 'none' acusa o RMW (§V6)"

  # devolve a arvore ao binario normal: os proximos a rodar a suite (e o
  # proprio usuario) nao esperam um binario 20x mais lento com TSan dentro.
  make clean >/dev/null 2>&1
  make >/dev/null 2>&1
fi

echo
echo "== S07: verificacao =="

# As cinco checagens deste estagio nao cobrem capacidade nova do programa:
# cobrem propriedades que o projeto vinha AFIRMANDO por argumento, em documento
# duravel, sem nunca medir. As cinco passaram na sondagem que precedeu o spec
# (§D2 de specs/S07-rede-de-verificacao.md) — o que elas impedem e que voltem a
# ser argumento, porque do lado de fora uma afirmacao verdadeira nao verificada
# e uma afirmacao falsa nao verificada sao indistinguiveis.
#
# Rodam DEPOIS de VS23/VS26 porque aquele bloco troca o binario pelo do
# ThreadSanitizer e so entao o reconstroi. Aqui o binario ja voltou ao normal,
# que e o que VS30 precisa: valgrind sobre o binario de producao, nao sobre um
# instrumentado por outro sanitizador.

# ---------------------------------------------------------------- VS28 ------
# V3 com as outras sementes.
#
# Todas as checagens de determinismo paralelo do projeto passam por $REF, que e
# --seed 1: VS19, VS20, VS21, VS24, VS25 e VS27. As outras quatro sementes que
# a suite usa (2, 7, 42, 43) so aparecem nos testes do gerador e de V4, sem
# threads. Ou seja: V3, a invariante central do trabalho, estava verificada sob
# threads em UMA semente.
#
# Uma semente diferente sorteia celulas diferentes, logo disputa alvos
# diferentes, logo exercita a regiao critica em outro padrao. Se o
# particionamento tivesse uma dependencia de ordem que a semente 1 nao revela,
# e aqui que ela apareceria.
echo
echo "VS28 V3 vale para as outras sementes, nao so para a --seed 1"
if [ ! -d "$DATA" ]; then
  fail "nao achei $DATA — rode 'make -C harness data'"
else
  vs28=0
  for s in 2 7 42 43; do
    SEED="--load $DATA --years 20 --seed $s --coeff 2,5,20,51,5 --critical-slope 15.0"
    $BIN $SEED --threads 1 --dump "$TMP/s$s-1.txt" >/dev/null 2>&1
    for n in 2 4 8; do
      $BIN $SEED --threads "$n" --dump "$TMP/s$s-$n.txt" >/dev/null 2>&1
      if ! cmp -s "$TMP/s$s-1.txt" "$TMP/s$s-$n.txt"; then
        fail "--seed $s --threads $n divergiu do serial da mesma semente"
        vs28=1
      fi
    done
  done
  [ "$vs28" -eq 0 ] && \
    pass "sementes 2, 7, 42 e 43: 2, 4 e 8 threads dao o z do serial"
fi

# ---------------------------------------------------------------- VS29 ------
# VS17 sob threads.
#
# O --scan reverse e o instrumento que §5 do HANDOFF elege como "o primeiro a
# usar quando algo divergir", porque separa dependencia de ORDEM de corrida de
# DADOS — dois problemas com solucoes diferentes. VS17 o exercita com uma
# thread, e o HANDOFF ARGUMENTAVA que ele "vale palavra por palavra
# paralelizado", com o raciocinio de que sob N threads o espelhamento apenas
# troca as faixas de dono e continua sendo uma particao.
#
# O raciocinio esta certo. Mas se ele estivesse errado, o instrumento de
# diagnostico do projeto e que estaria quebrado — e seria descoberto no dia em
# que fosse usado para diagnosticar outra coisa, que e o pior momento possivel.
#
# `legacy` NAO entra sob threads de proposito: ele e fluxo unico, nao paraleliza
# (é o oraculo de V7), entao exigir qualquer coisa dele com --threads 8 seria
# cobrar uma propriedade que ele nao tem. A guarda de dentes desta checagem
# continua sendo a de VS17, em serial, logo acima.
echo
echo "VS29 V4: inverter a varredura nao muda a saida, TAMBEM sob threads"
if [ ! -d "$DATA" ]; then
  fail "nao achei $DATA — rode 'make -C harness data'"
else
  vs29=0
  for n in 2 4 8; do
    $BIN $REF --rng anchored --scan forward --threads "$n" \
         --dump "$TMP/fwd$n.txt" >/dev/null 2>&1
    $BIN $REF --rng anchored --scan reverse --threads "$n" \
         --dump "$TMP/rev$n.txt" >/dev/null 2>&1
    if ! cmp -s "$TMP/fwd$n.txt" "$TMP/rev$n.txt"; then
      fail "--scan reverse divergiu com --threads $n — V4 nao vale paralelizado"
      vs29=1
    fi
  done
  [ "$vs29" -eq 0 ] && \
    pass "forward e reverse dao o mesmo z em 2, 4 e 8 threads (V4 sob threads)"
fi

# ---------------------------------------------------------------- VS30 ------
# Valgrind COM threads, nas tres variantes.
#
# VS2 roda valgrind com uma thread, porque e o que o alvo sempre fez. O efeito
# colateral e que o vetor de PAR_SHARDS mutexes da variante B — alocado em
# par_init e devolvido em par_finish — nunca tinha sido visto pelo memcheck:
# com --threads 1 nao ha pool, nem barreira, nem trava (par.c:170 rebaixa a
# variante a NONE), entao o caminho que aloca esse vetor nao executa.
#
# Um valgrind com threads foi rodado A MAO no fim do S05, antes de o vetor
# existir. Esta checagem existe para que isso nao dependa de alguem lembrar.
#
# A carga e o demo200, nao a 20x20 de VS2, e a escolha foi medida (§D3 do
# spec): as tres variantes custam 7,7 s no demo200 contra 6,0 s na 20x20 — 28 %
# mais caro —, e a 20x20 quase nao toma a trava, entao ela validaria a alocacao
# e nao o uso. O valgrind e dominado pela instrumentacao, nao pelo tamanho da
# grade, e por isso a carga forte sai quase de graca.
echo
echo "VS30 valgrind limpo COM --threads 8, nas variantes A, B e C"
if ! command -v valgrind >/dev/null 2>&1; then
  fail "valgrind nao instalado"
elif [ ! -d "$DATA" ]; then
  fail "nao achei $DATA — rode 'make -C harness data'"
else
  vs30=0
  for v in A B C; do
    if ! valgrind --error-exitcode=1 --leak-check=full \
                  --errors-for-leak-kinds=all \
                  $BIN $REF --threads 8 --variant "$v" \
                  >"$TMP/vg-$v.txt" 2>&1; then
      fail "valgrind acusou na variante $v com 8 threads"
      grep -E "ERROR SUMMARY|definitely lost|Invalid" "$TMP/vg-$v.txt" | head -5
      vs30=1
    fi
  done
  [ "$vs30" -eq 0 ] && \
    pass "A, B e C: 0 erros e 0 vazamentos sob 8 threads (o vetor de B incluso)"
fi

# ---------------------------------------------------------------- VS31 ------
# A faixa vazia, de verdade.
#
# par.c particiona [0, nrows) em nthreads faixas, e com nrows < nthreads algumas
# saem vazias (row_lo == row_hi). O comentario que descreve isso AFIRMAVA que o
# caso e exercitado "numa grade 20x20 com --threads 32, que os testes
# exercitam" — e nenhum teste passava de 8. Cobertura afirmada e nao conferida,
# num arquivo que par.h manda ler antes de mexer em thread.
#
# O comentario so voltou a dizer isso depois que esta linha passou a existir. E
# a ordem certa: o codigo descreve a rede que ha, nao a que se imagina.
#
# 64 esta na lista porque 20x20 com 64 threads deixa 44 workers com faixa vazia
# — tres vezes mais faixas vazias que preenchidas. Se houver divisao por zero,
# indice negativo ou barreira que conte errado, e aqui.
echo
echo "VS31 faixa vazia: 20x20 com 16, 32 e 64 threads (nrows < nthreads)"
$BIN --rows 20 --cols 20 --years 5 --seed 1 --threads 1 \
     --dump "$TMP/e1.txt" >/dev/null 2>&1
if [ ! -r "$TMP/e1.txt" ]; then
  fail "o serial de 20x20 nao produziu dump"
else
  vs31=0
  for n in 16 32 64; do
    if ! $BIN --rows 20 --cols 20 --years 5 --seed 1 --threads "$n" \
              --dump "$TMP/e$n.txt" >/dev/null 2>&1; then
      fail "--threads $n numa grade 20x20 saiu com erro"
      vs31=1
    elif ! cmp -s "$TMP/e1.txt" "$TMP/e$n.txt"; then
      fail "--threads $n numa grade 20x20 divergiu do serial"
      vs31=1
    fi
  done
  [ "$vs31" -eq 0 ] && \
    pass "16, 32 e 64 threads em 20 linhas: faixas vazias nao quebram V3"
fi

# ---------------------------------------------------------------- VS32 ------
# Mais threads que nucleos.
#
# §V3 e §B21 do mestre afirmam, os dois, que V3 foi verificada "em 1, 2, 4, 8 e
# 16 threads". VS19 itera `for n in 2 4 8`. O 16 foi rodado a mao no S05 e nunca
# virou suite: o invariante do mestre afirmava mais do que a rede sustentava.
#
# Nao se mexeu em VS19 para consertar isso, porque o que VS32 verifica e outra
# coisa — a maquina tem 12 nucleos, entao 16 e 32 threads sao SOBRESCRICAO, e o
# que se exercita e o escalonador entrelacando workers na mesma barreira. VS19
# continua sendo o caso comum e barato.
echo
echo "VS32 V3 com mais threads que nucleos (16 e 32, a maquina tem 12)"
if [ ! -r "$TMP/z1.txt" ]; then
  fail "o serial de referencia de VS19 nao esta disponivel"
else
  vs32=0
  for n in 16 32; do
    $BIN $REF --threads "$n" --dump "$TMP/z$n-over.txt" >/dev/null 2>&1
    if ! cmp -s "$TMP/z1.txt" "$TMP/z$n-over.txt"; then
      fail "--threads $n divergiu do serial — V3 cai em sobrescricao"
      vs32=1
    fi
  done
  [ "$vs32" -eq 0 ] && \
    pass "16 e 32 threads dao o z do serial: §V3 agora e medida, nao afirmacao"
fi

# ----------------------------------------------------------------------------
echo
if [ "$fails" -eq 0 ]; then
  if [ "$skips" -eq 0 ]; then
    echo "== tudo passou =="
  else
    echo "== tudo passou, com $skips verificacao(oes) adiada(s) para o S08 =="
  fi
  exit 0
else
  echo "== $fails falha(s), $skips adiada(s) =="
  exit 1
fi
