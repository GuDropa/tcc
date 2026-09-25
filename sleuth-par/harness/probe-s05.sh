#!/usr/bin/env bash
#
# probe-s05.sh — reparticao do tempo entre as fases de spr_spread.
#
# Responde, com numero em vez de intuicao, a pergunta que o spec do S05
# precisa responder antes de ser escrito: QUANTO do relogio esta na fase 4?
#
# Importa porque e o teto de Amdahl do estagio. Se a fase 4 vale 60 % do
# tempo, paraleliza-la PERFEITAMENTE limita o ganho total a 2,5x, e nenhuma
# quantidade de threads muda isso. Se vale 95 %, o teto e 20x e o estagio
# sozinho justifica o trabalho.
#
# Mede em varias dimensoes de grade de proposito: a fase 4 varre a grade
# inteira (custo O(total_pixels)) enquanto 1n3 e 5 fazem um numero FIXO de
# sondagens, entao a reparticao muda com o tamanho — e o que vale para o S08
# nao e o que vale para o demo200.
#
# Compila um binario separado com -DPROBE_PHASE_TIMING. O build normal nao
# carrega nada disto. Mesmo padrao de probe-t10.sh.
#
set -u
cd "$(dirname "$0")/.." || exit 1

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "== sonda S05: onde esta o tempo de spr_spread =="
echo

gcc -std=c11 -Wall -Wextra -O2 -pthread -UNDEBUG -DPROBE_PHASE_TIMING \
    -I. ./*.c -o "$TMP/sleuth-probe" -lm 2>"$TMP/build.txt" || {
  echo "a compilacao instrumentada falhou:"; cat "$TMP/build.txt"; exit 1; }

echo "--- 200x200 real (demo200), 20 anos, cenario de referencia ---"
"$TMP/sleuth-probe" --load data/demo200 --years 20 --seed 1 \
    --coeff 2,5,20,51,5 --critical-slope 15.0 >/dev/null

for dim in 500 1000 2000; do
  echo
  echo "--- ${dim}x${dim} artificial, 10 anos ---"
  "$TMP/sleuth-probe" --rows "$dim" --cols "$dim" --years 10 --seed 1 \
      --coeff 2,5,20,51,5 --critical-slope 15.0 >/dev/null
done

echo
echo "== leitura =="
echo "A fase 4 e a unica que varre a grade. Quanto maior a grade, maior a"
echo "fracao dela — e e justamente nas grades grandes que S08 mede speedup"
echo "(V15: nada de afirmacao de desempenho em grade pequena)."
