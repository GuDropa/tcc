#!/usr/bin/env bash
#
# grow-ref.sh — executa o `grow` de referencia.
#
#   ./grow-ref.sh predict CENARIO
#
# Existe por uma razao so: o SLEUTH de 2001 le cinco variaveis de ambiente que
# o csh da epoca definia sozinho e que um shell atual nao define. Sem elas o
# programa morre em SIGSEGV, e o handler de sinal imprime a lista de variaveis
# como se fosse o diagnostico:
#
#     main.c 789 Please make sure the following env variables are defined
#     main.c 791 USER -- set to your username
#     ...
#     main.c 817 caught signo SIGSEGV : Invalid storage access
#
# Atencao: essa mensagem NAO e a causa da falha, e so a dica que o handler
# sabe dar para qualquer SIGSEGV. Durante a sondagem do S03 as variaveis
# foram definidas e o programa continuou quebrando, por tres outros motivos
# (§B1, §B2, §B3 de specs/S03-harness.md). Ainda assim elas sao necessarias.
#
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
ref="$here/build/sleuth-ref"

[ -x "$ref/grow" ] || {
  echo "grow-ref: baseline nao construido. Rode 'make baseline'." >&2
  exit 1
}

export USER="${USER:-$(id -un)}"
export HOST="$(uname -n)"
export HOSTTYPE="${HOSTTYPE:-$(uname -m)}"
export OSTYPE="${OSTYPE:-linux}"

# O grow resolve WHIRLGIF_BINARY relativo ao diretorio de trabalho. Com
# ANIMATION=no ele nao e usado, mas rodar de dentro da arvore mantem o
# comportamento igual ao do SLEUTH original invocado como ./grow.
cd "$ref"
export PWD="$(pwd)"

exec ./grow "$@"
