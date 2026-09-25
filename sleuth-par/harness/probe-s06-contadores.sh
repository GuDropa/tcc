#!/usr/bin/env bash
#
# probe-s06-contadores.sh — §D5 previu que a variante C mudaria a REPARTICAO
# dos cinco contadores de tentativa. Previu-se; agora mede-se.
#
# O argumento de §D5: sob A e B, quem perde a disputa por um alvo e barrado na
# leitura de delta e nunca chega a avaliar a declividade, entao e contado em
# `ja crescida`. Sob C, quem perde avalia a cascata INTEIRA e so descobre a
# derrota no CAS — entao uma tentativa que sob A teria sido `ja crescida` pode
# virar `declividade` ou `area excluida`. A SOMA das tres continua exata, que e
# o que VS25 cobra e o que §B21 do mestre estabeleceu.
#
# Precisa da grade ladrilhada: no demo200 a trava e tomada 2 582 vezes em 20
# anos e as colisoes reais sao raras demais para a diferenca aparecer.
set -u
cd "$(dirname "$0")/.." || exit 1

REP=${REP:-5}
K=${K:-10}
LADRILHADO=${LADRILHADO:-data/demo$((200 * K))}
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

[ -f "$LADRILHADO/z.txt" ] || ./harness/mktiled.sh "$K" "$LADRILHADO" || exit 1

REF="--load $LADRILHADO --years 5 --seed 1 --coeff 2,5,20,51,5 --critical-slope 15.0"

echo "== §D5: a variante C muda a reparticao dos contadores? =="
echo "   $LADRILHADO, 5 anos, $REP execucoes por configuracao"
echo
printf '%-6s %-4s %-4s %9s %10s %9s %9s %9s %9s\n' \
  "var" "thr" "exec" "sucesso" "z_fail" "jacresc" "declivid" "excluida" "SOMA"
echo "---------------------------------------------------------------------------------"

for v in A B C; do
  for n in 1 8; do
    for rep in $(seq 1 "$REP"); do
      ./sleuth-par $REF --threads "$n" --variant "$v" > "$TMP/o.txt" 2>/dev/null
      awk -v v="$v" -v n="$n" -v r="$rep" '
        /^  sucesso/       { s  = $2 }
        /ja era urbana/    { z  = $5 }
        /ja crescida/      { d  = $4 }
        /^  declividade/   { sl = $2 }
        /area excluida/    { ex = $3 }
        END { printf "%-6s %-4d %-4d %9d %10d %9d %9d %9d %9d\n",
                     v, n, r, s, z, d, sl, ex, d + sl + ex }' "$TMP/o.txt"
    done
    [ "$n" = 1 ] || echo
  done
done

cat <<'FIM'
-- leitura --
As colunas 'sucesso', 'z_fail' e 'SOMA' tem de ser identicas em TODAS as
linhas — e o que §B21 estabeleceu e VS25 cobra. As colunas 'jacresc',
'declivid' e 'excluida' podem variar entre variantes E entre execucoes da
mesma variante; isso nao e V3 caindo, e a instrumentacao medindo a corrida de
que ela participa.

Se a reparticao de C for identica a de A, §D5 continua CERTO no argumento e a
diferenca so nao apareceu nesta entrada — a colisao real e rara mesmo aqui
(0,0026 tomadas por celula). Nao confunda "nao se observou" com "nao existe":
para afirmar que C muda a reparticao e preciso ve-la mudar.
FIM
