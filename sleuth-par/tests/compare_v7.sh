#!/usr/bin/env bash
#
# compare_v7.sh — VS11: a invariante V7 do spec mestre.
#
#   "A execucao serial do prototipo (1 thread) reproduz a saida do SLEUTH
#    original para a mesma entrada e a mesma semente."
#
# Roda os dois lados em 1990 -> 2010 sobre o demo200 e compara sete
# estatisticas por ano. Nao instrumenta o original: a superficie de comparacao
# e o avg.log que ele ja escreve com WRITE_AVG_FILE=yes e 1 Monte Carlo.
# Ver §D5 de specs/S03-harness.md.
#
#   ./tests/compare_v7.sh [--keep]
#
#     --keep   nao apaga os arquivos intermediarios; imprime onde ficaram
#
# O MAPEAMENTO DE COLUNAS NAO E O OBVIO. Duas armadilhas, ambas verificadas:
#
#   1. O `sdc` do prototipo sai na coluna `sdg` do avg.log ($5), nao em `sdc`
#      ($6). Causa: growth.c:213-216 chama stats_SetSDG duas vezes, porque
#      stats_SetSDC nao existe. A coluna `sdc` do avg.log e sempre zero.
#      (§B5 do spec do estagio.)
#
#   2. A coluna `slope` ($16) NAO e o average_slope que spr_spread devolve —
#      e a declividade media da area urbana inteira, calculada em
#      stats_circle (stats_obj.c:2653-2677). O average_slope de spr_spread e
#      recebido por growth.c:204 e nunca usado: nenhum stats_Set* o consome.
#      O prototipo imprime os dois; o comparavel e `decl_z`.
#
set -u

cd "$(dirname "$0")/.." || exit 1

keep=0
[ "${1:-}" = "--keep" ] && keep=1

par=./sleuth-par
harness=$(cd harness && pwd)
data=./data/demo200

# O cenario de referencia (§D4) fixa estes valores; o prototipo tem de receber
# os mesmos pela linha de comando, porque nao le arquivo de cenario.
#
# Sobrescreviveis pelo ambiente, e isso tem um uso concreto: alterar um deles
# SO no lado do prototipo e a forma de conferir que esta verificacao sabe
# falhar. Um comparador que nunca acusa nao verifica nada.
#
#     COEFF=3,5,20,51,5 ./tests/compare_v7.sh    # deve FALHAR
#
YEARS=${YEARS:-20}
SEED=${SEED:-1}
COEFF=${COEFF:-2,5,20,51,5}
CSLOPE=${CSLOPE:-15.0}
START=${START:-1990}
STOP=${STOP:-2010}

if [ "$keep" -eq 1 ]; then
  TMP=$harness/build/v7
  rm -rf "$TMP"; mkdir -p "$TMP"
else
  TMP=$(mktemp -d)
  trap 'rm -rf "$TMP"' EXIT
fi

die () { printf 'compare_v7: %s\n' "$1" >&2; exit 1; }

echo "== VS11: V7, prototipo serial contra o SLEUTH original =="
echo "   demo200 200x200 | $START -> $STOP | semente $SEED | coeff $COEFF | slope crit $CSLOPE"
echo

# ---------------------------------------------------------------------------
# 1. os dois binarios e os dados
# ---------------------------------------------------------------------------
[ -d "$data" ] || die "nao achei $data — rode 'make -C harness data'"

make >/dev/null 2>&1 || die "a compilacao do sleuth-par falhou"
[ -x "$par" ]        || die "nao achei $par"

make -C harness baseline >"$TMP/baseline.log" 2>&1 \
  || { tail -20 "$TMP/baseline.log"; die "a construcao do grow de referencia falhou"; }

# ---------------------------------------------------------------------------
# 2. o lado de referencia
# ---------------------------------------------------------------------------
refout=$harness/build/refout
scenario=$harness/build/scenario.ref

"$harness/mkscenario.sh" "$scenario" "$refout" "$STOP" >/dev/null \
  || die "mkscenario falhou"

rm -rf "$refout"; mkdir -p "$refout"
"$harness/grow-ref.sh" predict "$scenario" >"$TMP/grow.out" 2>&1 \
  || { tail -20 "$TMP/grow.out"; die "o grow de referencia abortou"; }

avg=$refout/avg.log
[ -r "$avg" ] || die "o grow nao escreveu $avg"

# ---------------------------------------------------------------------------
# 3. o prototipo
#
# --rng legacy E OBRIGATORIO AQUI, a partir do estagio S04.
#
# V7 diz que o prototipo serial reproduz o SLEUTH original. A partir do S04 o
# prototipo tem dois geradores, e so um deles e o ran1 do original. O modo
# `anchored`, que e o PADRAO, muda os numeros por construcao e nao por erro —
# um fluxo aleatorio diferente da estatisticas diferentes. Sem este `--rng
# legacy` explicito, este script compararia o gerador novo contra o avg.log do
# original e acusaria uma divergencia que nao existe.
#
# Dito de outro modo: a invariante V7 nao mudou, mudou o modo em que ela se
# verifica. Ver SPEC.md §V7 e §D7 de specs/S04-rng.md.
# ---------------------------------------------------------------------------
"$par" --load "$data" --years "$YEARS" --seed "$SEED" --rng legacy \
       --coeff "$COEFF" --critical-slope "$CSLOPE" >"$TMP/par.out" 2>&1 \
  || { tail -20 "$TMP/par.out"; die "o sleuth-par falhou"; }

# ---------------------------------------------------------------------------
# 4. normalizacao das duas saidas para a mesma tabela
#
#      ano sng sdc og rt cresc pop decl_z
#
# As seis primeiras estatisticas sao inteiras e a comparacao e exata. A
# setima, a declividade, NAO pode ser comparada por igualdade de string: o
# avg.log publica duas casas e o prototipo tem precisao total, entao o teste
# correto e "o valor do prototipo arredonda para o do avg.log", ou seja
# |dif| <= 0,005. Arredondar tambem o nosso lado antes de comparar ja produziu
# uma falha falsa — ver o comentario em main.c sobre o ano de 1997.
# ---------------------------------------------------------------------------

#   avg.log:  $2 year  $4 sng  $5 sdg(=sdc)  $6 sdc(=0)  $7 og  $8 rt
#             $9 pop   $16 slope  $27 grw_pix
awk '$1 ~ /^[0-9]+$/ {
       printf "%d %d %d %d %d %d %d %s\n",
              $2, $4, $5, $7, $8, $27, $9, $16
     }' "$avg" > "$TMP/ref.tab"

#   sleuth-par: $1 ano  $2 sng  $3 sdc  $4 og  $5 rt  $6 cresc  $7 pop
#               $8 cresc_decl  $9 decl_z
awk -v base="$START" '$1 ~ /^[0-9]+$/ && NF >= 9 {
       printf "%d %d %d %d %d %d %d %s\n",
              base + $1, $2, $3, $4, $5, $6, $7, $9
     }' "$TMP/par.out" > "$TMP/par.tab"

nref=$(wc -l < "$TMP/ref.tab")
npar=$(wc -l < "$TMP/par.tab")

fails=0

# ---------------------------------------------------------------------------
# 5. as tres verificacoes
# ---------------------------------------------------------------------------

# (a) mesmo numero de anos — senao a comparacao linha a linha nao significa nada
if [ "$nref" -ne "$YEARS" ] || [ "$npar" -ne "$YEARS" ]; then
  printf '  FALHA contagem de anos: referencia %s, prototipo %s, esperado %s\n' \
         "$nref" "$npar" "$YEARS"
  fails=$((fails + 1))
else
  printf '  ok    %s anos de cada lado\n' "$YEARS"
fi

# (b) a coluna sdc do avg.log tem de ser identicamente zero (§B5)
naozero=$(awk '$1 ~ /^[0-9]+$/ && $6 != 0 { print $2 }' "$avg")
if [ -n "$naozero" ]; then
  printf '  FALHA a coluna sdc do avg.log nao e zero nos anos: %s\n' "$naozero"
  printf '        §B5 previa zero sempre; se mudou, o mapeamento de §D5 caiu\n'
  fails=$((fails + 1))
else
  printf '  ok    coluna sdc do avg.log identicamente zero (§B5 confirmado)\n'
fi

# (c) as sete estatisticas, ano a ano
#
# Colunas 1..7 (ano e os seis inteiros): igualdade exata.
# Coluna 8 (declividade): |dif| <= 0,005, a precisao que o avg.log publica.
if paste "$TMP/ref.tab" "$TMP/par.tab" | awk '
     BEGIN { split("ano sng sdc og rt cresc pop decl_z", nome, " "); mostrados = 0 }
     {
       delete ruim
       nruim = 0
       for (k = 1; k <= 7; k++)
         if ($k + 0 != $(k + 8) + 0) ruim[++nruim] = k
       dif = $16 - $8
       if (dif < 0) dif = -dif
       if (dif > 0.005 + 1e-9) ruim[++nruim] = 8

       if (nruim > 0)
       {
         divergiu = 1
         if (mostrados < 3)
         {
           mostrados++
           printf "        ano %s\n", $1
           printf "          referencia "
           for (k = 2; k <= 8; k++) printf "%-9s", $k
           printf "\n          prototipo  "
           for (k = 10; k <= 16; k++) printf "%-9s", $k
           printf "\n          divergem:"
           for (n = 1; n <= nruim; n++)
             printf " %s (%s vs %s)", nome[ruim[n]], $(ruim[n]), $(ruim[n] + 8)
           printf "\n\n"
         }
       }
     }
     END { exit (divergiu ? 1 : 0) }
   ' > "$TMP/divergencias.txt"; then
  printf '  ok    sng sdc og rt cresc pop decl_z batem nos %s anos\n' "$YEARS"
  printf '        (declividade a menos de 0,005 — e o que o avg.log publica)\n'
else
  fails=$((fails + 1))
  printf '  FALHA as estatisticas divergem\n\n'
  printf '        %-11s' " " ; printf '%-9s' sng sdc og rt cresc pop decl_z
  printf '\n'
  cat "$TMP/divergencias.txt"
  printf '        ordem de suspeita em §V de specs/S03-harness.md:\n'
  printf '        1 mapeamento de colunas · 2 re-estampagem da semente (T6)\n'
  printf '        3 coeficientes/critical slope · 4 normalizacao de estradas\n'
  printf '        5 bug B7 do workspace · 6 util_get_next_neighbor\n'
fi

echo
if [ "$keep" -eq 1 ]; then
  echo "intermediarios em $TMP/  (ref.tab, par.tab, par.out, grow.out)"
  echo "avg.log de referencia em $avg"
  echo
fi

if [ "$fails" -eq 0 ]; then
  echo "== V7 verificada: o prototipo serial reproduz o SLEUTH original =="
  exit 0
else
  echo "== $fails falha(s) =="
  exit 1
fi
