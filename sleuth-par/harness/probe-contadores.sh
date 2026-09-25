#!/usr/bin/env bash
#
# probe-contadores.sh — os cinco contadores de tentativa de urbanizacao sob
# contencao. Sonda do S05, tarefa T6.
#
# A PERGUNTA. Com a variante A (mutex global) o `z` final e identico para
# qualquer numero de threads — isso VS19 confere. Mas os cinco contadores de
# spr_urbanize NAO sao: quem perde a corrida por uma celula-alvo e contado em
# `ja crescida (delta)` se chegou depois do vencedor, e em `declividade` se
# chegou antes e reprovou por conta propria. A exclusao mutua torna o MODELO
# independente da ordem; nao torna a INSTRUMENTACAO.
#
# Esta sonda mede tres coisas:
#   1. os contadores variam com o numero de threads?
#   2. variam entre execucoes com o MESMO numero de threads? (isto e: e
#      corrida de verdade, ou so uma funcao deterministica de N?)
#   3. o que se conserva? a hipotese e que urban_success e z_failure sao
#      invariantes, e que delta+slope+excluded tem SOMA invariante com
#      reparticao variavel.
#
set -u
cd "$(dirname "$0")/.." || exit 1

BIN=./sleuth-par
C="--load ./data/demo200 --years 20 --seed 1 --coeff 2,5,20,51,5 --critical-slope 15.0"
REP=${REP:-8}

# extrai os cinco contadores do bloco final, na ordem em que saem
contadores () {
  $BIN $C "$@" 2>/dev/null | awk '
    /^  sucesso/             { s=$2 }
    /^  ja era urbana/       { z=$5 }
    /^  ja crescida/         { d=$4 }
    /^  declividade/         { p=$2 }
    /^  area excluida/       { e=$3 }
    END { printf "%s %s %s %s %s %s\n", s, z, d, p, e, d+p+e }'
}

printf '%-22s %8s %8s %8s %8s %8s %10s\n' \
       "execucao" "sucesso" "z_fail" "delta_f" "slope_f" "excl_f" "d+s+e"
echo "------------------------------------------------------------------------------"

for n in 1 2 4 8 16; do
  for rep in $(seq 1 "$REP"); do
    linha=$(contadores --threads "$n" --variant A)
    printf '%-22s %8s %8s %8s %8s %8s %10s\n' \
           "A, $n thread(s) #$rep" $linha
  done
done

echo
echo "-- o mesmo com --variant none (SEM exclusao): a corrida de dados de verdade --"
for n in 4 8; do
  for rep in 1 2 3 4; do
    linha=$(contadores --threads "$n" --variant none)
    printf '%-22s %8s %8s %8s %8s %8s %10s\n' \
           "none, $n thread(s) #$rep" $linha
  done
done

echo
echo "-- e o z final diverge sem exclusao? --"
$BIN $C --threads 1 --variant A --dump /tmp/pc-serial.txt >/dev/null 2>&1
for v in A none; do
  difs=0
  for rep in 1 2 3 4 5 6; do
    $BIN $C --threads 8 --variant $v --dump "/tmp/pc-$v-$rep.txt" >/dev/null 2>&1
    cmp -s /tmp/pc-serial.txt "/tmp/pc-$v-$rep.txt" || difs=$((difs + 1))
  done
  echo "   variante $v, --threads 8: $difs de 6 execucoes divergiram do serial"
done
rm -f /tmp/pc-*.txt
