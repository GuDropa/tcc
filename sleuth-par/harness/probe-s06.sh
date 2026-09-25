#!/usr/bin/env bash
#
# probe-s06.sh — a sondagem que escreveu o spec do S06 (§R1: sondar antes de
# catalogar). Reexecutavel; os numeros de §D de specs/S06-regiao-critica.md
# saem daqui.
#
# TRES PERGUNTAS, e as tres precisam de fracao urbana REAL em grade GRANDE —
# que e por que este arquivo comeca ladrilhando o demo200 (harness/mktiled.sh)
# em vez de usar o testgen:
#
#   1. A trava global custa alguma coisa? Mede-se comparando --variant A
#      contra --variant none, que e o MESMO binario sem trava nenhuma e
#      portanto o piso teorico das variantes B e C.
#   2. Quanto da fase 5 e a montagem da lista O(N), e quanto e a caminhada
#      pela estrada que e sequencial por construcao?
#   3. §B23 do mestre — "paralelizar as varreduras deixa as fases seriais mais
#      lentas", +43 % na contagem de populacao — se reproduz?
#
# Sobre a pergunta 2: abrir a fase 5 em duas exige um slot de sonda a mais em
# spread.c. Em vez de mexer na arvore antes de o estagio comecar (§R1), o
# script COPIA a arvore para um temporario e instrumenta a copia. A arvore de
# trabalho nao e tocada.
#
set -u
cd "$(dirname "$0")/.." || exit 1

REP=${REP:-5}
K=${K:-10}                       # 10 -> 2000x2000
LADRILHADO=${LADRILHADO:-data/demo$((200 * K))}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "== sondagem do S06: a regiao critica, a fase 5 e §B23 =="
echo "   mediana de $REP, clock_gettime(CLOCK_MONOTONIC), I/O fora (V11)"

# --- a entrada honesta ------------------------------------------------------
if [ ! -f "$LADRILHADO/z.txt" ]; then
  echo
  echo "-- ladrilhando o demo200 (nao existia) --"
  ./harness/mktiled.sh "$K" "$LADRILHADO" || exit 1
fi

# --- binario instrumentado: sonda padrao ------------------------------------
gcc -std=c11 -Wall -Wextra -O2 -pthread -UNDEBUG -DPROBE_PHASE_TIMING \
    -I. ./*.c -o "$TMP/probe" -lm 2>"$TMP/b.err" || {
  echo "a compilacao instrumentada falhou:"; cat "$TMP/b.err"; exit 1; }

# --- binario instrumentado: fase 5 aberta em duas ---------------------------
# O slot NOVO recebe "entrada da fase 5 ate o fim da montagem da lista"; o slot
# da fase 5, que era a fase inteira, passa a conter so o resto — a caminhada.
#
# O indice do slot novo e PROBE_SLOTS, isto e, o primeiro livre — e por isso
# ele e calculado do arquivo e nao escrito a mao. Estava escrito a mao ("8"), e
# quando o T1 do S06 derrubou a sonda de 8 para 7 slots (a contagem de
# populacao deixou de ser passada separada, §D6) a marca passou a cair FORA do
# vetor: escrita fora dos limites, silenciosa, dentro do instrumento que existe
# para medir. As duas guardas abaixo fecham essa porta.
cp -r . "$TMP/tree" 2>/dev/null

NSLOTS=$(awk '/^#define PROBE_SLOTS /{ print $3; exit }' spread.c)
case "$NSLOTS" in
  ''|*[!0-9]*) echo "nao achei PROBE_SLOTS em spread.c"; exit 1 ;;
esac

awk -v ns="$NSLOTS" '
  /^#define PROBE_SLOTS / {
    printf "#define PROBE_SLOTS %d   /* ... + montagem da lista da fase 5 */\n", ns + 1
    next }
  /"merge"};/ {
    sub(/"merge"};/, "\"merge\", \"f5:lista\"};"); print; next }
  /^  assert \(growth_count <= total_pixels\);/ {
    print
    print ""
    printf "  PROBE_MARCA (%d);   /* sonda do S06: fim da montagem da lista */\n", ns
    next }
  { print }
' spread.c > "$TMP/tree/spread.c"

grep -q "PROBE_MARCA ($NSLOTS)" "$TMP/tree/spread.c" || {
  echo "a instrumentacao da fase 5 nao pegou — a ancora mudou em spread.c"; exit 1; }
grep -q '"f5:lista"' "$TMP/tree/spread.c" || {
  echo "o rotulo do slot novo nao entrou — o vetor de rotulos mudou de forma."
  echo "Sem ele a marca cairia fora do vetor de slots. Ver o comentario acima."
  exit 1; }

gcc -std=c11 -Wall -Wextra -O2 -pthread -UNDEBUG -DPROBE_PHASE_TIMING \
    -I"$TMP/tree" "$TMP/tree"/*.c -o "$TMP/probe5" -lm 2>"$TMP/b5.err" || {
  echo "a compilacao da sonda da fase 5 falhou:"; cat "$TMP/b5.err"; exit 1; }

etapa ()   { awk -v r="$2" '$1 == r || ($1 " " $2) == r { for (i = 1; i <= NF; i++)
               if ($i == "ms") { print $(i-1); exit } }' "$1"; }
mediana () { printf '%s\n' "$@" | sort -n | awk '{v[NR]=$1} END {print v[int((NR+1)/2)]}'; }

REF_DEMO200="--load data/demo200 --years 20 --seed 1 --coeff 2,5,20,51,5 --critical-slope 15.0"
REF_LADR="--load $LADRILHADO --years 5 --seed 1 --coeff 2,5,20,51,5 --critical-slope 15.0"
REF_ARTIF="--rows $((200 * K)) --cols $((200 * K)) --years 5 --seed 1 --coeff 2,5,20,51,5 --critical-slope 15.0"

# ---------------------------------------------------------------------------
# 1. o custo da trava: A contra none
# ---------------------------------------------------------------------------
# A ORDEM DAS VARIANTES ROTACIONA A CADA REPETICAO, e isto nao e capricho.
#
# §B2 do spec do estagio mediu, neste mesmo projeto e nesta mesma maquina, que
# duas execucoes em sequencia nao sao comparaveis: a segunda sai sistematica-
# mente mais lenta, e a 8 threads o artefato chegou a 19 ms — quase 20 % do
# ano, medido em codigo IDENTICO. Uma tabela de quatro variantes medidas sempre
# na ordem A, B, C, none daria a A uma vantagem estrutural e a none uma
# desvantagem, e none e justamente o PISO contra o qual as outras tres sao
# julgadas. O achado sairia invertido e pareceria legitimo.
#
# Rotacionar faz cada variante ocupar cada posicao da sequencia em igual
# numero de repeticoes, entao o efeito de posicao entra igual em todas e sai
# na comparacao. Use REP multiplo de 4.
VARIANTES=(A B C none)

variantes () {   # <rotulo> <args...>
  local rot=$1; shift
  echo
  echo "--- $rot ---"
  printf '%-7s %26s   %26s\n' "" "-------- fase 4 --------" "------ ano inteiro -----"
  printf '%-7s %7s %7s %7s %7s  %7s %7s %7s %7s\n' \
    "threads" "A" "B" "C" "none" "A" "B" "C" "none"
  echo "--------------------------------------------------------------------------"
  for n in 1 2 4 8; do
    local f_A=() f_B=() f_C=() f_N=()
    local t_A=() t_B=() t_C=() t_N=()
    for rep in $(seq 0 $((REP - 1))); do
      local k
      for k in 0 1 2 3; do
        local v=${VARIANTES[$(( (k + rep) % 4 ))]}
        "$TMP/probe" "$@" --threads "$n" --variant "$v" >/dev/null 2>"$TMP/v.txt"
        local f t
        f=$(etapa "$TMP/v.txt" 'fase 4'); t=$(etapa "$TMP/v.txt" total)
        case "$v" in
          A)    f_A+=("$f"); t_A+=("$t") ;;
          B)    f_B+=("$f"); t_B+=("$t") ;;
          C)    f_C+=("$f"); t_C+=("$t") ;;
          none) f_N+=("$f"); t_N+=("$t") ;;
        esac
      done
    done
    printf '%-7s %7s %7s %7s %7s  %7s %7s %7s %7s\n' "$n" \
      "$(mediana "${f_A[@]}")" "$(mediana "${f_B[@]}")" \
      "$(mediana "${f_C[@]}")" "$(mediana "${f_N[@]}")" \
      "$(mediana "${t_A[@]}")" "$(mediana "${t_B[@]}")" \
      "$(mediana "${t_C[@]}")" "$(mediana "${t_N[@]}")"
  done
  # a linha que responde a pergunta do estagio: a distancia ate o piso
  printf '%-7s %7s %7s %7s %7s  %7s %7s %7s %7s\n' "custo" \
    "$(awk -v a="$(mediana "${f_A[@]}")" -v p="$(mediana "${f_N[@]}")" \
         'BEGIN { printf "%+.0f%%", 100*(a-p)/p }')" \
    "$(awk -v a="$(mediana "${f_B[@]}")" -v p="$(mediana "${f_N[@]}")" \
         'BEGIN { printf "%+.0f%%", 100*(a-p)/p }')" \
    "$(awk -v a="$(mediana "${f_C[@]}")" -v p="$(mediana "${f_N[@]}")" \
         'BEGIN { printf "%+.0f%%", 100*(a-p)/p }')" "(piso)" \
    "$(awk -v a="$(mediana "${t_A[@]}")" -v p="$(mediana "${t_N[@]}")" \
         'BEGIN { printf "%+.0f%%", 100*(a-p)/p }')" \
    "$(awk -v a="$(mediana "${t_B[@]}")" -v p="$(mediana "${t_N[@]}")" \
         'BEGIN { printf "%+.0f%%", 100*(a-p)/p }')" \
    "$(awk -v a="$(mediana "${t_C[@]}")" -v p="$(mediana "${t_N[@]}")" \
         'BEGIN { printf "%+.0f%%", 100*(a-p)/p }')" "(piso)"
  echo "  (a linha 'custo' e sobre o piso, e vale para a ultima linha de threads)"
}

echo
echo "############ 1. quanto custa cada variante da regiao critica? ############"
echo "  fase 4, em ms, mediana de $REP. 'none' e o MESMO binario sem exclusao"
echo "  nenhuma: e o piso teorico das tres, e nao uma quarta opcao — com mais"
echo "  de uma thread ele e corrida de dados (VS26 o acusa de proposito)."
echo "  Com --threads 1 as quatro degeneram no mesmo serial (VS27), entao a"
echo "  primeira linha mede o ruido: ±1-2 %."
#
# GRADES escolhe quais entradas medir. Existe para a remedicao de §B25: quem
# for conferir estes numeros em outra sessao raramente precisa das tres, e
# `GRADES=ladrilhado` corta o tempo da sondagem para um terco. O padrao roda
# as tres, porque e a comparacao entre elas que sustenta §D3.
#
GRADES=${GRADES:-"ladrilhado artificial demo200"}

para_grade () {   # <funcao> — chama-a uma vez por grade pedida
  local fn=$1 g
  for g in $GRADES; do
    case "$g" in
      ladrilhado) "$fn" "LADRILHADO ${LADRILHADO##*/} — fracao urbana REAL" \
                        $REF_LADR ;;
      artificial) "$fn" "artificial — grade VAZIA, a trava quase nao e tomada" \
                        $REF_ARTIF ;;
      demo200)    "$fn" "demo200 200x200 — real, mas o pool domina" \
                        $REF_DEMO200 ;;
      *) echo "GRADES: nao conheco '$g' (use ladrilhado, artificial, demo200)"
         exit 1 ;;
    esac
  done
}

para_grade variantes

# ---------------------------------------------------------------------------
# 2. e 3. a tabela por etapa, com a fase 5 aberta
# ---------------------------------------------------------------------------
tabela () {   # <rotulo> <args...>
  local rot=$1; shift
  echo
  echo "--- $rot ---"
  # A coluna 'pop' saiu no T1 do S06: a contagem de populacao foi fundida no
  # laco do merge (§D6) e nao ha mais passada separada para medir. Quem quiser
  # o antes/depois dessa mudanca usa harness/probe-s06-t1.sh, que compara duas
  # arvores. Aqui ela apareceria vazia, que e pior que nao aparecer.
  printf '%-8s %7s %7s %7s %8s %9s %9s %7s %7s %9s\n' \
    "threads" "pool" "zera" "1n3" "fase4" "f5:lista" "f5:andar" "filtr" \
    "merge" "total"
  echo "--------------------------------------------------------------------------------------"
  for n in 1 2 4 8; do
    local po=() z=() a=() f4=() fl=() fa=() ft=() mg=() tt=()
    for rep in $(seq 1 "$REP"); do
      "$TMP/probe5" "$@" --threads "$n" --variant A >/dev/null 2>"$TMP/r.txt"
      po+=("$(etapa "$TMP/r.txt" pool)");         z+=("$(etapa "$TMP/r.txt" 'zera delta')")
      a+=("$(etapa "$TMP/r.txt" 'fase 1n3')");   f4+=("$(etapa "$TMP/r.txt" 'fase 4')")
      fl+=("$(etapa "$TMP/r.txt" 'f5:lista')");  fa+=("$(etapa "$TMP/r.txt" 'fase 5')")
      ft+=("$(etapa "$TMP/r.txt" filtros)");     mg+=("$(etapa "$TMP/r.txt" merge)")
      tt+=("$(etapa "$TMP/r.txt" total)")
    done
    printf '%-8s %7s %7s %7s %8s %9s %9s %7s %7s %9s\n' \
      "$n" "$(mediana "${po[@]}")" "$(mediana "${z[@]}")" "$(mediana "${a[@]}")" \
      "$(mediana "${f4[@]}")" "$(mediana "${fl[@]}")" "$(mediana "${fa[@]}")" \
      "$(mediana "${ft[@]}")" "$(mediana "${mg[@]}")" \
      "$(mediana "${tt[@]}")"
  done
}

echo
echo "############ 2. a fase 5: quanto e lista O(N), quanto e caminhada? ############"
echo "############ 3. §B23: as etapas SERIAIS pioram com mais threads?  ############"
echo "  O controle limpo de §B23 era a 'pop', medida fora de qualquer barreira."
echo "  Ela nao existe mais (T1 do S06, §D6) — para reler §B23 contra esta"
echo "  sonda, rode-a no commit anterior ao T1. O que sobra aqui de serial e"
echo "  'f5:lista', que esta DENTRO das barreiras e por isso e controle pior."
para_grade tabela

# ---------------------------------------------------------------------------
# 4. quantas vezes a trava e tomada, por celula varrida
# ---------------------------------------------------------------------------
echo
echo "############ 4. a trava e tomada com que frequencia? ############"
echo "  par_lock() roda uma vez por spr_urbanize com z == 0, isto e:"
echo "  sucesso + delta + declividade + excluida. O z_failure fica FORA."
echo
printf '%-30s %10s %12s %12s\n' "entrada" "travas" "celulas" "travas/cel"
echo "-------------------------------------------------------------------"
conta () {   # <rotulo> <celulas> <anos> <args...>
  local rot=$1 cel=$2 anos=$3; shift 3
  ./sleuth-par "$@" --threads 1 --variant A > "$TMP/o.txt" 2>&1
  awk -v r="$rot" -v c="$cel" -v a="$anos" '
    /^  sucesso/       { s  = $2 }
    /ja crescida/      { d  = $4 }
    /^  declividade/   { sl = $2 }
    /area excluida/    { ex = $3 }
    END { t = s + d + sl + ex
          printf "%-30s %10d %12d %12.6f\n", r, t, c * a, t / (c * a) }' "$TMP/o.txt"
}
conta "ladrilhado, 5 anos"    $((200 * K * 200 * K)) 5  $REF_LADR
conta "artificial, 5 anos"    $((200 * K * 200 * K)) 5  $REF_ARTIF
conta "demo200, 20 anos"      40000                  20 $REF_DEMO200

# ---------------------------------------------------------------------------
# 5. quantas travas a variante B precisa ter
# ---------------------------------------------------------------------------
# PAR_SHARDS e sobrescritivel na compilacao justamente para esta secao existir:
# o numero de travas de B e uma escolha, e uma escolha sem medida e um chute
# com comentario bonito em volta.
#
# A linha de PAR_SHARDS=1 e a GUARDA da secao: com uma trava so, B e a variante
# A por construcao, e tem de custar o mesmo que ela. Se ela nao custar, o
# mecanismo de B nao e o que este arquivo diz que e — o indice pode estar
# errado, ou a trava pode nao estar sendo tomada.
echo
echo "############ 5. de quantas travas a variante B precisa? ############"
echo "  fase 4 a 8 threads, mediana de $REP, na grade ladrilhada."
echo "  PAR_SHARDS=1 TEM de reproduzir a variante A: com uma trava so, B e A."
echo
printf '%-12s %10s %10s\n' "PAR_SHARDS" "fase 4" "vs. piso"
echo "--------------------------------------"

piso_f4=$(
  for rep in $(seq 1 "$REP"); do
    "$TMP/probe" $REF_LADR --threads 8 --variant none >/dev/null 2>"$TMP/p.txt"
    etapa "$TMP/p.txt" 'fase 4'
  done | sort -n | awk '{v[NR]=$1} END {print v[int((NR+1)/2)]}')

for s in ${SHARDS_SWEEP:-1 8 64 1024 16384}; do
  gcc -std=c11 -Wall -Wextra -O2 -pthread -UNDEBUG -DPROBE_PHASE_TIMING \
      -DPAR_SHARDS="$s" -I. ./*.c -o "$TMP/sh" -lm 2>"$TMP/se.txt" || {
    printf '%-12s %10s %10s\n' "$s" "nao compila" "(veja se e potencia de 2)"
    continue; }
  v=()
  for rep in $(seq 1 "$REP"); do
    "$TMP/sh" $REF_LADR --threads 8 --variant B >/dev/null 2>"$TMP/s.txt"
    v+=("$(etapa "$TMP/s.txt" 'fase 4')")
  done
  m=$(mediana "${v[@]}")
  printf '%-12s %10s %10s\n' "$s" "$m" \
    "$(awk -v a="$m" -v p="$piso_f4" 'BEGIN { printf "%+.0f%%", 100*(a-p)/p }')"
done
printf '%-12s %10s %10s\n' "(piso, none)" "$piso_f4" "--"

cat <<'FIM'

-- leitura --
1. Se o custo sobre 'none' for grande no LADRILHADO e ~0 no artificial, a
   diferenca e a fracao urbana: na grade vazia a fase 4 curto-circuita e a
   trava quase nao e tomada, entao a grade artificial NAO mede a regiao
   critica (§B20/§B24). A coluna que responde "B e C valeram o esforco?" e a
   distancia de cada uma ate 'none', nao a distancia entre elas e A.
2. f5:andar e a parte sequencial por construcao da fase 5; f5:lista e a
   varredura O(N) escondida. A proporcao entre as duas decide se vale
   paralisar a lista.
3. §B23 nao e mais respondivel aqui: o controle que ele elegeu era a 'pop',
   que o T1 eliminou. §B25 ja o retirou do texto; §B2 do spec do S06 oferece
   um mecanismo candidato (efeito de ordem dentro da sequencia de medicao) e
   diz como testa-lo — embaralhar a ordem da matriz de threads.

V15: nenhuma afirmacao de speedup do TRABALHO sai daqui — isso e o S08. O que
sai daqui e a decisao de recorte do S06.
FIM
