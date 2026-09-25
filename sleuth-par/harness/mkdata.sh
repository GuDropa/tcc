#!/usr/bin/env bash
#
# mkdata.sh — converte as grades do demo200 para texto, no formato que o
#             --load do sleuth-par consome.
#
#   ./mkdata.sh [DIRETORIO_DE_SAIDA]        (padrao: ../data/demo200)
#
# O gif2asc le o GIF e nada mais: o valor que ele emite e o componente
# vermelho da cor indexada, exatamente como gdif_obj.c:369. As
# transformacoes que o SLEUTH aplica DEPOIS da leitura entram aqui, para que
# o leitor continue sendo so um leitor.
#
# Quais transformacoes existem, de fato (sondagem de §D3):
#
#   - estradas: igrid_NormalizeRoads (main.c:394). A UNICA grade que o
#     original transforma.
#   - urbano -> z: util_init_grid(z,0) seguido de
#     util_condition_gif(semente, GT, 0, z, PHASE0G), em growth.c:103-116.
#   - excluded e slope: CRUAS. igrid_ValidateGrids e igrid_VerifyInputs
#     apenas conferem e registram; nao mutam nada.
#
# Quais grades, e por que uma so de cada:
#
#   Em modo predict o original carrega apenas as grades urbanas com ano >=
#   PREDICTION_START_DATE (igrid_obj.c:1237), e — por conta do bug de §B4 —
#   exatamente uma grade de estrada. Confirmado instrumentando a copia:
#   urban_count=1 (urban.1990), road_count=1 (roads.1990).
#
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
tcc=$(cd "$here/../.." && pwd)

gif2asc="$here/build/gif2asc"
indir="$tcc/SLEUTH/Input/demo200"
outdir="${1:-$here/../data/demo200}"

[ -x "$gif2asc" ] || { echo "mkdata: rode 'make gif2asc' primeiro" >&2; exit 1; }
[ -d "$indir" ]   || { echo "mkdata: nao achei $indir" >&2; exit 1; }

mkdir -p "$outdir"

PHASE0G=3          # globals.h:44 e SLEUTH/ugm_defines.h:61

# ----------------------------------------------------------------------------
# copia crua: excluded e slope
# ----------------------------------------------------------------------------
raw () {
  local src=$1 dst=$2
  "$gif2asc" "$indir/$src" > "$outdir/$dst"
  printf '  %-14s <- %-28s cru\n' "$dst" "$src"
}

# ----------------------------------------------------------------------------
# semente urbana -> z
#
#   util_condition_gif (total_pixels, seed, GT, 0, z, PHASE0G)
#
# grava PHASE0G onde a semente e > 0, deixando o resto no zero que
# util_init_grid pos la.
# ----------------------------------------------------------------------------
seed_to_z () {
  local src=$1 dst=$2
  "$gif2asc" "$indir/$src" \
    | awk -v p="$PHASE0G" '
        NR == 1 { print; next }
        {
          for (j = 1; j <= NF; j++) $j = ($j > 0) ? p : 0
          print
        }' > "$outdir/$dst"
  printf '  %-14s <- %-28s PHASE0G(%s) onde > 0\n' "$dst" "$src" "$PHASE0G"
}

# ----------------------------------------------------------------------------
# estradas -> normalizadas
#
#   igrid_obj.c:722-727
#     image_max   = igrid.road[i].max;
#     norm_factor = image_max / (float) max_of_max;
#     grid_ptr[j] = (PIXEL) (((100.0 * grid_ptr[j]) / image_max) * norm_factor);
#
# Com uma unica grade carregada, max_of_max == image_max e norm_factor == 1.0.
# O cast para PIXEL (long) trunca; int() do awk tambem trunca para zero.
#
# E daqui que sai a resposta da questao 4 da sessao 01: com image_max = 255,
# toda celula de estrada vira exatamente 100, entao a divisao INTEIRA de
# `roads[...] / MAX_ROAD_VALUE` em spr_road_walk da 1 e run_value acaba
# valendo o coeficiente de difusao. A divisao inteira fica mascarada pela
# normalizacao — ver §B9.
# ----------------------------------------------------------------------------
roads_normalized () {
  local src=$1 dst=$2
  local tmp; tmp=$(mktemp)
  "$gif2asc" "$indir/$src" > "$tmp"

  local image_max
  image_max=$(awk 'NR > 1 { for (j = 1; j <= NF; j++) if ($j > m) m = $j } END { print m+0 }' "$tmp")
  [ "$image_max" -gt 0 ] || { echo "mkdata: $src tem maximo zero" >&2; exit 1; }

  awk -v mx="$image_max" '
    NR == 1 { print; next }
    {
      for (j = 1; j <= NF; j++) $j = int((100.0 * $j) / mx)   # norm_factor = 1.0
      print
    }' "$tmp" > "$outdir/$dst"
  rm -f "$tmp"
  printf '  %-14s <- %-28s normalizada (max=%s -> 100)\n' "$dst" "$src" "$image_max"
}

echo "convertendo o demo200 para $outdir"
seed_to_z        demo200.urban.1990.gif  z.txt
roads_normalized demo200.roads.1990.gif  roads.txt
raw              demo200.excluded.gif    excld.txt
raw              demo200.slope.gif       slp.txt

# ----------------------------------------------------------------------------
# procedencia: de onde veio cada arquivo e o que foi feito com ele.
# ----------------------------------------------------------------------------
cat > "$outdir/PROCEDENCIA.md" <<EOF
# demo200 — grades em texto

Geradas por \`harness/mkdata.sh\` a partir de \`SLEUTH/Input/demo200/\`.
**Não editar à mão**: rode \`make -C harness data\`.

Formato: primeira linha \`nrows ncols\`, depois \`nrows\` linhas de
\`ncols\` inteiros separados por espaço. É o mesmo formato que o
\`--dump\` do \`sleuth-par\` produz.

| arquivo | origem | transformação |
|---|---|---|
| \`z.txt\` | \`demo200.urban.1990.gif\` | \`PHASE0G\` (=$PHASE0G) onde > 0 — \`util_condition_gif(GT, 0)\` |
| \`roads.txt\` | \`demo200.roads.1990.gif\` | \`igrid_NormalizeRoads\`: \`(long)(100.0*v/max)\`, \`norm_factor = 1.0\` |
| \`excld.txt\` | \`demo200.excluded.gif\` | crua |
| \`slp.txt\` | \`demo200.slope.gif\` | crua |

Uma grade de cada porque é o que o original carrega em modo \`predict\`:
apenas as urbanas com ano >= \`PREDICTION_START_DATE\` (\`igrid_obj.c:1237\`),
e exatamente uma de estrada por conta do defeito de §B4.

## Conferência

$(cd "$outdir" && sha256sum z.txt roads.txt excld.txt slp.txt | sed 's/^/    /')

A procedência é identificada por soma de verificação, não por data de
geração: assim rodar \`make -C harness data\` sobre as mesmas entradas não
suja a árvore de trabalho, e um diff neste arquivo significa que os **dados**
mudaram.
EOF

echo
echo "resumo dos arquivos gerados:"
for f in z.txt roads.txt excld.txt slp.txt; do
  awk -v n="$f" '
    NR == 1 { rows = $1; cols = $2; next }
    { for (j = 1; j <= NF; j++) { c[$j]++; if ($j > 0) nz++ } }
    END {
      d = 0; for (k in c) d++
      printf "  %-10s %sx%s  nao-zero=%-6d distintos=%d\n", n, rows, cols, nz+0, d
    }' "$outdir/$f"
done
