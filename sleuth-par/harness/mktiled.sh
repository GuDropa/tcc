#!/usr/bin/env bash
#
# mktiled.sh — ladrilha o demo200 K x K vezes, preservando a fracao urbana.
#
# POR QUE ISTO EXISTE. §B20 do mestre registra a divida: o gerador artificial
# do testgen nao produz cidade — a 1000x1000, mesmo com 150 anos, a grade
# chega a 0,28 % urbana contra os 9,70 % iniciais do demo200. Como a fase 4
# curto-circuita em celula nao urbana, toda medicao em grade artificial grande
# mede uma grade VAZIA, nao uma grade GRANDE. §B24 estreitou o problema (as
# outras tres varreduras fazem o mesmo trabalho por pixel numa grade vazia),
# mas nao o quitou: qualquer numero que dependa de haver cidade — a fase 4, e
# sobretudo o custo da REGIAO CRITICA — continuava sem entrada honesta.
#
# A alternativa barata estava levantada desde o S05 e nunca testada: em vez de
# escrever um gerador que produza cidade, REPETIR a cidade que ja existe. O
# S06 testou e funciona — a fracao urbana sai identica a do original, digito
# por digito, porque ladrilhar e uma operacao exata sobre a grade.
#
# O ARTEFATO CONHECIDO, e ele e real: as estradas nao se conectam atravessando
# a costura entre ladrilhos. Cada ladrilho fica com a malha viaria do demo200
# isolada da dos vizinhos. Isso afeta o MODELO (a fase 5 cresce ao longo de
# estradas), nao o PERFIL — e a caminhada pela estrada custa 0,17 ms de 16,65
# na fase 5 a 2000x2000, ou seja 1 %. Para medir tempo, serve. Para afirmar
# qualquer coisa sobre o comportamento urbano, nao serve, e nao e para isso
# que foi feito.
#
#   uso:  harness/mktiled.sh [K] [destino]
#   ex.:  harness/mktiled.sh 10                -> data/demo2000, 2000x2000
#
set -u
cd "$(dirname "$0")/.." || exit 1

K=${1:-10}
ORIG=data/demo200
DEST=${2:-data/demo$((200 * K))}

if [ ! -d "$ORIG" ]; then
  echo "nao achei $ORIG — rode 'make -C harness data' antes" >&2
  exit 1
fi
case "$K" in (*[!0-9]*|"") echo "K tem de ser inteiro positivo" >&2; exit 1;; esac
[ "$K" -ge 1 ] || { echo "K tem de ser >= 1" >&2; exit 1; }

mkdir -p "$DEST"

for nome in z slp excld roads; do
  awk -v k="$K" '
    NR == 1 { printf "%d %d\n", $1 * k, $2 * k; next }
    { linhas[++n] = $0 }
    END {
      for (t = 0; t < k; t++)
        for (i = 1; i <= n; i++) {
          linha = linhas[i]
          saida = linha
          for (j = 1; j < k; j++) saida = saida " " linha
          print saida
        }
    }' "$ORIG/$nome.txt" > "$DEST/$nome.txt"
done

# --- confere que ladrilhar preservou o que se queria preservar -------------
frac () {
  awk 'NR > 1 { for (i = 1; i <= NF; i++) { t++; if ($i > 0) u++ } }
       END { printf "%.2f%%", 100 * u / t }' "$1"
}
fo=$(frac "$ORIG/z.txt")
fd=$(frac "$DEST/z.txt")

cat > "$DEST/PROCEDENCIA.md" <<EOF
# $DEST — demo200 ladrilhado ${K}x${K}

Gerado por \`harness/mktiled.sh $K\`, a partir de \`$ORIG\`. **Derivado: nao
versionar, reconstruir com o script.**

Grade $((200 * K))x$((200 * K)). Fracao urbana inicial $fd, identica a do
original ($fo) — ladrilhar e exato.

Serve para MEDIR TEMPO em grade grande com fracao urbana realista, que e o que
§B20 do mestre registra como divida e o que a grade artificial do testgen nao
da. **Nao serve para afirmar nada sobre comportamento urbano:** as estradas nao
atravessam a costura entre ladrilhos.
EOF

echo "ladrilhado ${K}x${K} -> $DEST ($((200 * K))x$((200 * K)))"
echo "fracao urbana inicial: original $fo, ladrilhado $fd"
if [ "$fo" != "$fd" ]; then
  echo "AVISO: as fracoes diferem — ladrilhar deveria ser exato, investigue" >&2
  exit 1
fi
