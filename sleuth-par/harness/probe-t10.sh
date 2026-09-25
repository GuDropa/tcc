#!/usr/bin/env bash
#
# probe-t10.sh — sonda descartavel: o _Static_assert de globals.h sabe falhar?
#
# Um assert que nao pode disparar nao garante nada. Esta sonda copia a arvore,
# troca `#define PIXEL long` por `int` (que e exatamente a mudanca que §B8
# preve) e confere que a compilacao QUEBRA, citando o motivo.
#
# Nao entra em run_checks.sh: e verificacao da verificacao, roda uma vez.
#
set -u

cd "$(dirname "$0")/.." || exit 1

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "== sonda T10: o _Static_assert de globals.h dispara? =="
echo

cp *.c *.h Makefile "$TMP/"
sed -i 's/^#define PIXEL             long$/#define PIXEL             int/' "$TMP/globals.h"

if ! grep -q '^#define PIXEL             int$' "$TMP/globals.h"; then
  echo "  FALHA o sed nao pegou — a linha do #define PIXEL mudou de forma?"
  exit 1
fi

if make -C "$TMP" >"$TMP/build.log" 2>&1; then
  echo "  FALHA com PIXEL=int a compilacao PASSOU — o assert nao protege nada"
  exit 1
fi

if grep -q "static assertion failed" "$TMP/build.log"; then
  echo "  ok    com PIXEL=int a compilacao quebra no _Static_assert:"
  grep -m1 -A1 "static assertion failed" "$TMP/build.log" | sed 's/^/          /'
  echo
  echo "== o assert de §B8 sabe falhar =="
  exit 0
else
  echo "  FALHA quebrou, mas nao no _Static_assert — outro erro veio antes:"
  grep -m5 "error:" "$TMP/build.log" | sed 's/^/          /'
  exit 1
fi
