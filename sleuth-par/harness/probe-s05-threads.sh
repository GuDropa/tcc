#!/usr/bin/env bash
#
# probe-s05-threads.sh — o estagio S05 funcionou? (tarefa T8)
#
# ISTO NAO E A MEDICAO DE DESEMPENHO DO TRABALHO. Essa e o estagio S08, com
# grades grandes e fracao urbana realista. O demo200 tem 200x200 = 40 000
# pixels e o ano inteiro de spr_spread custa ~6 ms serial; V15 do mestre
# proibe afirmar speedup sobre grade pequena, e §B20 explica por que a
# alternativa obvia (gerar uma grade artificial grande) mede uma grade VAZIA
# em vez de uma grade grande.
#
# O que este numero responde e outra coisa, e e o que a T8 pede: as threads
# estao de fato dividindo trabalho, ou o estagio entregou paralelismo
# decorativo? Para isso o demo200 serve, porque e a unica entrada com fracao
# urbana real (14,2 %) — e a fase 4 curto-circuita em celula nao urbana.
#
# Mede com -DPROBE_PHASE_TIMING, que cronometra DENTRO de spr_spread com
# clock_gettime(CLOCK_MONOTONIC) e portanto exclui I/O e montagem das grades,
# como V11 exige. Reporta a MEDIANA de REP repeticoes.
#
# ⚠ A COLUNA 'pop' SAI VAZIA A PARTIR DO S06 (T1). Nao e defeito desta sonda:
# a contagem de populacao deixou de ser uma passada separada e foi fundida no
# laco do merge (§D6 do spec do S06), entao o slot que esta coluna lia nao
# existe mais. O script fica como esta, sem remendo, porque e o instrumento
# com que §B22 e §B25 foram medidos e a reproducao daqueles numeros depende
# de ele nao mudar — rode-o contra o commit correspondente. Para o antes/
# depois do T1, o instrumento e harness/probe-s06-t1.sh.
#
set -u
cd "$(dirname "$0")/.." || exit 1

REP=${REP:-7}
ANOS=${ANOS:-20}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "== S05 T8: as threads dividem trabalho? demo200, $ANOS anos, mediana de $REP =="
echo

gcc -std=c11 -Wall -Wextra -O2 -pthread -UNDEBUG -DPROBE_PHASE_TIMING \
    -I. ./*.c -o "$TMP/probe" -lm 2>"$TMP/build.txt" || {
  echo "a compilacao instrumentada falhou:"; cat "$TMP/build.txt"; exit 1; }

# a sonda imprime uma linha por etapa e uma linha "total", em stderr
etapa () {   # <arquivo> <rotulo>
  awk -v r="$2" '$1 == r || ($1 " " $2) == r { for (i=1;i<=NF;i++)
      if ($i == "ms") { print $(i-1); exit } }' "$1"
}

mediana () { printf '%s\n' "$@" | sort -n | awk '{v[NR]=$1} END {print v[int((NR+1)/2)]}'; }

tabela () {   # <rotulo do caso> <args de entrada...>
  local caso=$1; shift
  local base="" base4=""
  echo
  echo "--- $caso ---"
  printf '%-9s %8s %8s %8s %8s %8s %8s %8s %8s %10s %9s %8s\n' \
    "threads" "pool" "zera" "1n3" "fase4" "fase5" "filtros" "merge" "pop" \
    "total(ms)" "4 varred." "ganho4"
  echo "--------------------------------------------------------------------------------------------------------------"
  for n in 1 2 4 8; do
    local tots=() po=() z=() f1=() f4=() f5=() fl=() mg=() pp=() quatro=()
    for rep in $(seq 1 "$REP"); do
      "$TMP/probe" "$@" --threads "$n" --variant A >/dev/null 2>"$TMP/r.txt"
      tots+=("$(etapa "$TMP/r.txt" total)")
      po+=("$(etapa "$TMP/r.txt" pool)")
      z+=("$(etapa "$TMP/r.txt" "zera delta")")
      f1+=("$(etapa "$TMP/r.txt" "fase 1n3")")
      f4+=("$(etapa "$TMP/r.txt" "fase 4")")
      f5+=("$(etapa "$TMP/r.txt" "fase 5")")
      fl+=("$(etapa "$TMP/r.txt" filtros)")
      mg+=("$(etapa "$TMP/r.txt" merge)")
      pp+=("$(etapa "$TMP/r.txt" pop)")
      # a soma das QUATRO varreduras do §S — o que este estagio se propos a
      # paralelizar. O resto (pool, 1n3, fase5, pop) e overhead ou serial.
      quatro+=("$(awk -v a="$(etapa "$TMP/r.txt" 'zera delta')" \
                      -v b="$(etapa "$TMP/r.txt" 'fase 4')" \
                      -v c="$(etapa "$TMP/r.txt" filtros)" \
                      -v d="$(etapa "$TMP/r.txt" merge)" \
                      'BEGIN { printf "%.2f", a+b+c+d }')")
    done
    local t q g4
    t=$(mediana "${tots[@]}")
    q=$(mediana "${quatro[@]}")
    [ -z "$base" ] && { base=$t; base4=$q; }
    g4=$(awk -v b="$base4" -v q="$q" 'BEGIN { printf "%.2fx", b/q }')
    printf '%-9s %8s %8s %8s %8s %8s %8s %8s %8s %10s %9s %8s\n' \
      "$n" "$(mediana "${po[@]}")" "$(mediana "${z[@]}")" "$(mediana "${f1[@]}")" \
      "$(mediana "${f4[@]}")" "$(mediana "${f5[@]}")" "$(mediana "${fl[@]}")" \
      "$(mediana "${mg[@]}")" "$(mediana "${pp[@]}")" "$t" "$q" "$g4"
  done
}

tabela "demo200 200x200 real, $ANOS anos (14,2 % urbana)" \
       --load data/demo200 --years "$ANOS" --seed 1 \
       --coeff 2,5,20,51,5 --critical-slope 15.0

# Grades grandes artificiais. §B20 do mestre avisa que elas nao produzem
# cidade — a 1000x1000 a grade chega a 0,28 % urbana — e conclui que medir
# perfil nelas mede uma grade VAZIA.
#
# Isso vale para a FASE 4, que curto-circuita em celula nao urbana. NAO vale
# para as outras tres varreduras: zerar delta, os dois filtros e o merge
# percorrem total_pixels e fazem o mesmo trabalho por pixel havendo cidade ou
# nao. Entao a coluna fase4 aqui e para ignorar, e as outras tres sao dado
# legitimo — que e o unico jeito que este estagio tem de ver as varreduras
# trabalhando em escala sem esperar o testgen do S08. Ver §B23.
for dim in 1000 2000; do
  tabela "${dim}x${dim} artificial, 5 anos (fase4 NAO representativa, §B20/§B23)" \
         --rows "$dim" --cols "$dim" --years 5 --seed 1 \
         --coeff 2,5,20,51,5 --critical-slope 15.0
done

echo
echo "-- leitura --"
echo "pool          = pthread_create das N-1 threads, uma vez por ano (§T1)."
echo "1n3/fase5/pop = SERIAIS neste estagio; se encolherem, o numero esta errado."
echo "4 varred.     = zera + fase4 + filtros + merge, que e o que o §S promete"
echo "                paralelizar. 'ganho4' e o speedup DELAS, sem o overhead."
echo
echo "V15: nenhuma afirmacao de speedup do TRABALHO sai daqui. O capitulo de"
echo "desempenho e o S08, com fracao urbana realista em grade grande."
