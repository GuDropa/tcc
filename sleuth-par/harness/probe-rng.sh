#!/usr/bin/env bash
#
# probe-rng.sh — sondagem para o spec do S04 (§R1: sondar antes de catalogar).
#
# Responde, por MEDICAO e nao por leitura de codigo, tres perguntas que o
# desenho do RNG ancorado a celula depende:
#
#   1. quantos sorteios cada fase consome por ano, no cenario de referencia;
#   2. qual a participacao de cada fase no total — isto e, onde o gerador
#      ancorado precisa ser rapido;
#   3. quantos sorteios uma celula da fase 4 pode consumir (o numero de
#      "subpassos" que a chave do gerador precisa reservar).
#
# Metodo: copia a arvore para um temporario, instrumenta random.c com um
# contador global e spr_spread com uma leitura do contador em volta de cada
# fase. Nao toca na arvore de trabalho.
#
set -u

cd "$(dirname "$0")/.." || exit 1

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

DATA=$(pwd)/data/demo200
cp *.c *.h Makefile "$TMP/"

# ---------------------------------------------------------------------------
# 1. contador global no gerador
# ---------------------------------------------------------------------------
python3 - "$TMP/random.c" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding='utf-8', errors='surrogateescape').read()

s = s.replace('RANDOM_SEED_TYPE ran_seed;',
              'RANDOM_SEED_TYPE ran_seed;\nunsigned long g_ran_calls = 0;', 1)

alvo = '''  ran_random (RANDOM_SEED_TYPE *ran_idum)
{
'''
if alvo not in s:
    sys.exit('probe-rng: nao achei o corpo de ran_random')
s = s.replace(alvo, alvo + '  g_ran_calls++;\n', 1)

open(p, 'w', encoding='utf-8', errors='surrogateescape').write(s)
PY

# ---------------------------------------------------------------------------
# 2. leitura do contador em volta de cada fase
# ---------------------------------------------------------------------------
python3 - "$TMP/spread.c" <<'PY'
import sys, re
p = sys.argv[1]
s = open(p, encoding='utf-8', errors='surrogateescape').read()

s = s.replace('void   spr_spread (',
 '''extern unsigned long g_ran_calls;
unsigned long g_rng_p13 = 0, g_rng_p4 = 0, g_rng_p5 = 0;

void   spr_spread (''', 1)

# As CHAMADAS das tres fases dentro de spr_spread sao as unicas ocorrencias
# com recuo de exatamente dois espacos: o prototipo e a definicao comecam em
# "void". Por isso o \n  ancora sem ambiguidade — anexar texto a assinatura,
# como tentei antes, pegava a declaracao no topo do arquivo.
for fase, acc in (('spr_phase1n3', 'g_rng_p13'),
                  ('spr_phase4',   'g_rng_p4'),
                  ('spr_phase5',   'g_rng_p5')):
    pat = r'\n  ' + fase + r' \(.*?\);'
    m = re.search(pat, s, re.S)
    if not m:
        sys.exit('probe-rng: nao achei a chamada de %s em spr_spread' % fase)
    s = (s[:m.start()]
         + '\n  { unsigned long _a = g_ran_calls;'
         + m.group(0)
         + '\n  ' + acc + ' += g_ran_calls - _a; }'
         + s[m.end():])

open(p, 'w', encoding='utf-8', errors='surrogateescape').write(s)
PY

# ---------------------------------------------------------------------------
# 3. relatorio ao fim da execucao
# ---------------------------------------------------------------------------
python3 - "$TMP/main.c" <<'PY'
import sys, re
p = sys.argv[1]
s = open(p, encoding='utf-8', errors='surrogateescape').read()
# imprime o resumo imediatamente antes do grid_free() final de main()
alvo = '  grid_free ();'
i = s.rfind(alvo)
if i < 0:
    sys.exit('probe-rng: nao achei o ponto de insercao em main.c')
extra = '''  {
    extern unsigned long g_ran_calls, g_rng_p13, g_rng_p4, g_rng_p5;
    fprintf (stderr, "RNGPROBE total=%lu p13=%lu p4=%lu p5=%lu\\n",
             g_ran_calls, g_rng_p13, g_rng_p4, g_rng_p5);
  }
'''
s = s[:i] + extra + s[i:]
open(p, 'w', encoding='utf-8', errors='surrogateescape').write(s)
PY

# ---------------------------------------------------------------------------
if ! make -C "$TMP" >"$TMP/build.log" 2>&1; then
  echo "FALHA: a arvore instrumentada nao compilou"
  grep -m10 -E "error:" "$TMP/build.log"
  exit 1
fi

echo "== sonda S04: consumo de sorteios por fase =="
echo "   demo200 200x200 | 1990->2010 | semente 1 | coeff 2,5,20,51,5"
echo

out=$("$TMP/sleuth-par" --load "$DATA" --years 20 --seed 1 \
        --coeff 2,5,20,51,5 --critical-slope 15.0 2>&1 >/dev/null \
      | grep RNGPROBE)

echo "$out" | awk '{
  for (i = 1; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] }
  t = v["total"]
  printf "   total dos 20 anos   %10d sorteios  (%.0f por ano)\n", t, t/20
  printf "   fase 1n3 (difusao)  %10d   %5.1f %%\n", v["p13"], 100*v["p13"]/t
  printf "   fase 4  (organico)  %10d   %5.1f %%\n", v["p4"],  100*v["p4"]/t
  printf "   fase 5  (estradas)  %10d   %5.1f %%\n", v["p5"],  100*v["p5"]/t
  printf "   fora das fases      %10d   %5.1f %%\n", t - v["p13"] - v["p4"] - v["p5"], \
         100*(t - v["p13"] - v["p4"] - v["p5"])/t
}'

echo
echo "   celulas varridas pela fase 4 por ano: 198*198 = 39204"
echo "$out" | awk '{
  for (i = 1; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] }
  printf "   sorteios da fase 4 por celula varrida: %.3f\n", v["p4"]/20/39204
}'
