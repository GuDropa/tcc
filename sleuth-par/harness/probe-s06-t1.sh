#!/usr/bin/env bash
#
# probe-s06-t1.sh — quanto valeu fundir a contagem de populacao no merge.
#
# A pergunta e "o T1 do S06 (§D6) pagou?", e a unica forma honesta de
# responde-la e comparar DUAS ARVORES NA MESMA SESSAO. §B25 do mestre existe
# por causa disso: os absolutos deste projeto ja variaram 25-30 % de uma sessao
# para a outra na mesma maquina, e foi assim que §B23 virou achado e depois
# caiu. Comparar o depois medido hoje contra o antes tabelado em §D2, que saiu
# de ontem, seria repetir o erro exato que o estagio documenta.
#
# Por isso o "antes" nao e um numero guardado: e um BINARIO, construido aqui,
# a partir de um commit. O padrao e HEAD, que e o pre-T1 enquanto o T1 nao
# estiver commitado; depois disso, passe o SHA:
#
#     REF=e53c2f3 ./harness/probe-s06-t1.sh
#
# O "depois" e a arvore de trabalho como ela esta agora.
#
# NOTA SOBRE AS COLUNAS. O binario antigo tem oito slots de sonda e o novo tem
# sete: "pop" deixou de existir como passada separada, que e o ponto do T1. Por
# isso a comparacao e no TOTAL e no MERGE — o merge e quem absorveu o trabalho,
# e o total e quem tem de encolher.
set -u
cd "$(dirname "$0")/.." || exit 1

REF=${REF:-HEAD}
REP=${REP:-5}
K=${K:-10}                       # 10 -> 2000x2000
LADRILHADO=${LADRILHADO:-data/demo$((200 * K))}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "== S06 T1: a contagem de populacao fundida no merge (§D6) =="
echo "   antes = $REF   ·   depois = arvore de trabalho"
echo "   mediana de $REP, clock_gettime(CLOCK_MONOTONIC), I/O fora (V11)"

# --- a entrada honesta (§D1): fracao urbana real em grade grande ------------
if [ ! -f "$LADRILHADO/z.txt" ]; then
  echo
  echo "-- ladrilhando o demo200 (nao existia) --"
  ./harness/mktiled.sh "$K" "$LADRILHADO" || exit 1
fi

# --- o "antes", extraido do commit ------------------------------------------
mkdir -p "$TMP/antes"
git -C .. archive "$REF" sleuth-par | tar -x -C "$TMP/antes" || {
  echo "nao consegui extrair $REF — o commit existe?"; exit 1; }

grep -q 'util_count_pixels (y.total_pixels' "$TMP/antes/sleuth-par/spread.c" || {
  echo "ERRO: $REF ja nao tem a varredura de populacao separada."
  echo "      O 'antes' tem de ser um commit ANTERIOR ao T1 — passe o SHA em REF."
  exit 1; }

gcc -std=c11 -Wall -Wextra -O2 -pthread -UNDEBUG -DPROBE_PHASE_TIMING \
    -I"$TMP/antes/sleuth-par" "$TMP/antes/sleuth-par"/*.c \
    -o "$TMP/antes-probe" -lm 2>"$TMP/e1" || {
  echo "a compilacao do 'antes' falhou:"; cat "$TMP/e1"; exit 1; }

# --- o "depois", da arvore de trabalho --------------------------------------
gcc -std=c11 -Wall -Wextra -O2 -pthread -UNDEBUG -DPROBE_PHASE_TIMING \
    -I. ./*.c -o "$TMP/depois-probe" -lm 2>"$TMP/e2" || {
  echo "a compilacao do 'depois' falhou:"; cat "$TMP/e2"; exit 1; }

etapa ()   { awk -v r="$2" '$1 == r || ($1 " " $2) == r { for (i = 1; i <= NF; i++)
               if ($i == "ms") { print $(i-1); exit } }' "$1"; }
mediana () { printf '%s\n' "$@" | sort -n | awk '{v[NR]=$1} END {print v[int((NR+1)/2)]}'; }

REF_LADR="--load $LADRILHADO --years 5 --seed 1 --coeff 2,5,20,51,5 --critical-slope 15.0"
REF_DEMO200="--load data/demo200 --years 20 --seed 1 --coeff 2,5,20,51,5 --critical-slope 15.0"

# UMA execucao imprime TODOS os slots, entao colher uma coluna por execucao
# seria rodar cinco vezes o que uma resolve. As repeticoes sao do relogio, nao
# das colunas.
#
# E O TOTAL NAO E A COLUNA QUE RESPONDE. O ganho esperado do T1 e ~13 ms de um
# ano de ~110, isto e, 12 % — e a variacao entre execucoes da mesma
# configuracao neste projeto e de 10 a 20 %. Ler o efeito no total e ler o
# efeito dentro do ruido, que foi exatamente como §B23 nasceu.
#
# Entao a tabela traz um CONTROLE. O T1 nao tocou em nada fora do merge: zerar
# delta, 1n3, fase 4, fase 5 e filtros sao byte a byte o mesmo codigo nas duas
# arvores. A soma deles ('resto') TEM de ser igual nos dois binarios, e o que
# ela acusar de diferenca e a medida direta do ruido da sessao. O efeito de
# verdade e local e se calcula das partes:
#
#     ganho = pop(antes) - [merge(depois) - merge(antes)]
#
# ou seja, a passada que sumiu menos o que a comparacao por pixel acrescentou
# ao merge. Se |ruido| for da ordem do ganho, o total nao sustenta afirmacao
# nenhuma — e as partes ainda sustentam.
tabela () {   # <rotulo> <args...>
  local rot=$1; shift
  echo
  echo "--- $rot ---"
  printf '%-7s %8s %9s %9s %8s %9s %9s %9s %8s\n' \
    "threads" "pop(a)" "merge(a)" "merge(d)" "d.merge" "GANHO" "resto(a)" "resto(d)" "ruido"
  echo "----------------------------------------------------------------------------------"
  for n in 1 2 4 8; do
    local pa=() ma=() md=() ra=() rd=()
    for rep in $(seq 1 "$REP"); do
      #
      # A ORDEM DENTRO DA REPETICAO IMPORTA, e descobriu-se medindo. Com
      # 'antes' sempre primeiro, a coluna 'ruido' — que compara codigo
      # IDENTICO nos dois binarios e deveria dar zero — saiu sistematicamente
      # positiva na grade grande. Ou a segunda execucao de cada par e mais
      # lenta por alguma razao de maquina, ou o efeito e real; ORDEM=inverte
      # troca quem vai primeiro e separa as duas hipoteses. Se o sinal do
      # 'ruido' acompanhar a ordem, e artefato de ordem.
      #
      if [ "${ORDEM:-direta}" = "inverte" ]; then
        "$TMP/depois-probe" "$@" --threads "$n" --variant A >/dev/null 2>"$TMP/d.txt"
        "$TMP/antes-probe"  "$@" --threads "$n" --variant A >/dev/null 2>"$TMP/a.txt"
      else
        "$TMP/antes-probe"  "$@" --threads "$n" --variant A >/dev/null 2>"$TMP/a.txt"
        "$TMP/depois-probe" "$@" --threads "$n" --variant A >/dev/null 2>"$TMP/d.txt"
      fi
      local p m t
      p=$(etapa "$TMP/a.txt" pop); m=$(etapa "$TMP/a.txt" merge)
      t=$(etapa "$TMP/a.txt" total)
      pa+=("$p"); ma+=("$m")
      ra+=("$(awk -v t="$t" -v m="$m" -v p="$p" 'BEGIN { printf "%.2f", t-m-p }')")
      m=$(etapa "$TMP/d.txt" merge); t=$(etapa "$TMP/d.txt" total)
      md+=("$m")
      rd+=("$(awk -v t="$t" -v m="$m" 'BEGIN { printf "%.2f", t-m }')")
    done
    local x_pa x_ma x_md x_ra x_rd
    x_pa=$(mediana "${pa[@]}"); x_ma=$(mediana "${ma[@]}"); x_md=$(mediana "${md[@]}")
    x_ra=$(mediana "${ra[@]}"); x_rd=$(mediana "${rd[@]}")
    printf '%-7s %8s %9s %9s %8s %9s %9s %9s %8s\n' "$n" "$x_pa" "$x_ma" "$x_md" \
      "$(awk -v a="$x_ma" -v d="$x_md" 'BEGIN { printf "%+.2f", d-a }')" \
      "$(awk -v p="$x_pa" -v a="$x_ma" -v d="$x_md" 'BEGIN { printf "%.2f ms", p-(d-a) }')" \
      "$x_ra" "$x_rd" \
      "$(awk -v a="$x_ra" -v d="$x_rd" 'BEGIN { printf "%+.2f", d-a }')"
  done
}

tabela "LADRILHADO ${LADRILHADO##*/} — fracao urbana REAL, 5 anos" $REF_LADR
tabela "demo200 200x200, 20 anos"                                  $REF_DEMO200

# --- e a prova de que a fusao nao mudou o modelo ----------------------------
echo
echo "############ o numero de populacao e o MESMO? ############"
echo "  VS21 ja cobre isto contra o golden; aqui e a confirmacao direta entre"
echo "  os dois binarios desta sessao, e na grade grande, que o golden nao tem."
for n in 1 8; do
  "$TMP/antes-probe"  $REF_LADR --threads "$n" --variant A 2>/dev/null > "$TMP/pa.txt"
  "$TMP/depois-probe" $REF_LADR --threads "$n" --variant A 2>/dev/null > "$TMP/pd.txt"
  if diff <(tail -n +2 "$TMP/pa.txt") <(tail -n +2 "$TMP/pd.txt") >/dev/null; then
    echo "  ok    $n thread(s): saida textual identica (inclui a coluna pop)"
  else
    echo "  FALHA $n thread(s): a fusao MUDOU a saida — §D6 esta errado"
    diff <(tail -n +2 "$TMP/pa.txt") <(tail -n +2 "$TMP/pd.txt") | head -10
  fi
done

cat <<'FIM'

-- leitura --
O T1 nao paralelizou nada: eliminou uma passada de leitura da grade inteira.
Entao o ganho e (a) aproximadamente o custo de 'pop(a)' menos o que a
comparacao por pixel acrescentou ao merge, e (b) presente tambem com UMA
thread — e essa e a diferenca entre este ganho e todos os do S05, que so
aparecem com N > 1. Uma etapa que deixa de existir nao precisa de threads.

LEIA A COLUNA 'ruido' ANTES DE QUALQUER OUTRA. Ela compara codigo identico
nos dois binarios e deveria dar zero. O que ela der e o piso de incerteza
desta sessao. Se |ruido| >= GANHO, nenhuma afirmacao sobre o TOTAL do ano se
sustenta — e a afirmacao sobre a ETAPA continua se sustentando, porque
pop(a) e d.merge sao medidas locais e nao razoes entre totais.

E §B25 continua valendo por cima de tudo isto: o que sai daqui so vira
afirmacao no texto depois de ser remedido em OUTRA sessao.

V15: isto NAO e afirmacao de speedup do trabalho. E o custo de uma etapa
medido contra ela mesma. O speedup e o S08.
FIM
