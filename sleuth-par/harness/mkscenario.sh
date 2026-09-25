#!/usr/bin/env bash
#
# mkscenario.sh — gera o cenario de referencia para a comparacao de V7.
#
#   ./mkscenario.sh SAIDA_DO_CENARIO DIRETORIO_DE_SAIDA [ANO_FINAL]
#
# Derivado de SLEUTH/Scenarios/scenario.demo200_predict. Cada ajuste existe
# por um motivo, e tres deles contornam defeitos do SLEUTH original. Ver §D4
# de specs/S03-harness.md.
#
# O cenario original nao e editado (§R2): ele e lido e a versao ajustada sai
# em outro arquivo.
#
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
tcc=$(cd "$here/../.." && pwd)

src="$tcc/SLEUTH/Scenarios/scenario.demo200_predict"
out="${1:?uso: mkscenario.sh SAIDA DIRETORIO_DE_SAIDA [ANO_FINAL]}"
outdir="${2:?falta o diretorio de saida}"
stop="${3:-2010}"

[ -r "$src" ] || { echo "mkscenario: nao achei $src" >&2; exit 1; }

# O INPUT_DIR precisa de barra no fim: o SLEUTH concatena direto o nome do
# arquivo (igrid_obj.c:1239-1240, strcpy + strcat sem separador).
indir="$tcc/SLEUTH/Input/demo200/"
case "$outdir" in */) ;; *) outdir="$outdir/" ;; esac

grep -v "^LANDUSE_DATA" "$src" | sed \
  `#  --- caminhos: o cenario original usa relativos a SLEUTH/ --- ` \
  -e "s|^INPUT_DIR=.*|INPUT_DIR=$indir|" \
  -e "s|^OUTPUT_DIR=.*|OUTPUT_DIR=$outdir|" \
  \
  `#  --- 1 Monte Carlo: o avg.log passa a ser a corrida unica, ` \
  `#      nao a media de 10 execucoes ---                        ` \
  -e "s|^MONTE_CARLO_ITERATIONS=.*|MONTE_CARLO_ITERATIONS=1|" \
  \
  `#  --- neutraliza coeff_SelfModication (growth.c:262), que reajusta ` \
  `#      os coeficientes a cada ano. A receita vem COMENTADA no proprio ` \
  `#      cenario, linhas 428-429. Verificavel na saida: as colunas ` \
  `#      diffus/spread/breed/slp_res/rd_grav do avg.log ficam constantes ` \
  -e "s|^CRITICAL_LOW=.*|CRITICAL_LOW=0.0|" \
  -e "s|^CRITICAL_HIGH=.*|CRITICAL_HIGH=10000000000000.0|" \
  \
  `#  --- contorna o fclose(NULL) de mem_Init: com WRITE_MEMORY_MAP=NO o ` \
  `#      memlog_fp fica NULL e o fclose nao tem guarda (§B2) ---        ` \
  -e "s|^WRITE_MEMORY_MAP(YES/NO)=.*|WRITE_MEMORY_MAP(YES/NO)=YES|" \
  \
  `#  --- contorna o estouro de buffer de landclass_LogIt (§B3) ---      ` \
  -e "s|^LOGGING(YES/NO)=.*|LOGGING(YES/NO)=NO|" \
  \
  `#  --- a superficie de comparacao de V7 ---                           ` \
  -e "s|^WRITE_AVG_FILE(YES/NO)=.*|WRITE_AVG_FILE(YES/NO)=yes|" \
  -e "s|^WRITE_COEFF_FILE(YES/NO)=.*|WRITE_COEFF_FILE(YES/NO)=yes|" \
  \
  `#  --- ruido e dependencia externa (whirlgif) ---                     ` \
  -e "s|^ECHO(YES/NO)=.*|ECHO(YES/NO)=no|" \
  -e "s|^ANIMATION(YES/NO)=.*|ANIMATION(YES/NO)=no|" \
  -e "s|^ECHO_IMAGE_FILES(YES/NO)=.*|ECHO_IMAGE_FILES(YES/NO)=no|" \
  \
  -e "s|^PREDICTION_STOP_DATE=.*|PREDICTION_STOP_DATE=$stop|" \
  > "$out"

# As linhas LANDUSE_DATA foram removidas para cair no caminho
# grw_non_landuse (growth.c:239) e nao no deltatron, que §C exclui.
if grep -q "^LANDUSE_DATA" "$out"; then
  echo "mkscenario: LANDUSE_DATA sobrou no cenario gerado" >&2
  exit 1
fi

echo "cenario de referencia: $out  (1990 -> $stop, saida em $outdir)"
