#!/usr/bin/env bash
#
# probe-s08-ruido.sh — a sondagem que decide o recorte do S08 (§R1: sondar
# antes de catalogar). Reexecutavel.
#
# A PERGUNTA, e ela e uma so:
#
#   §B29 mediu 4,6 % de espalhamento no TOTAL do ano com codigo byte a byte
#   identico (as quatro variantes a 1 thread degeneram no mesmo serial, VS27,
#   e deram 180,08 / 187,08 / 179,89 / 178,89 ms). A mesma execucao reproduziu
#   a FASE 4 dentro de 2 %. A regra que saiu disso — "compare etapas, nunca
#   totais" — protege o texto, mas o §T do mestre manda o S08 publicar
#   desempenho, e desempenho e numero de ano inteiro.
#
#   Entao: de ONDE vem essa variancia, e mais repeticao a mata?
#
# O QUE TORNA A PERGUNTA RESPONDIVEL. O "total" da sonda nao e um cronometro
# independente: e a SOMA dos sete slots, somada em spread.c:111. Nao ha residuo
# nao marcado onde a variancia possa se esconder. Logo, se a fase 4 espalha 2 %
# e o total espalha 4,6 %, existe pelo menos um outro slot espalhando MAIS que
# o total — e da para apontar qual. Esta sonda aponta.
#
# DUAS COISAS SAO MEDIDAS, e sao diferentes:
#
#   1. ESPALHAMENTO por slot — repetindo a MESMA configuracao N vezes. §B29
#      comparou quatro variantes que por acaso sao o mesmo codigo; aqui e o
#      mesmo comando, o que tira qualquer duvida sobre o binario.
#   2. DERIVA — a mediana da primeira metade das repeticoes contra a da
#      segunda. Este e o discriminador, e e ele que decide o recorte:
#
#        . espalhamento ALEATORIO  -> repetir resolve, e T8 (--repeat N
#          cronometrando dentro do processo, V11) e o caminho. O S08 e a
#          tabela de §T, com o instrumento consertado antes.
#        . DERIVA monotonica       -> repetir NAO resolve, so move a deriva
#          para dentro do processo. O S08 tem de mudar de forma: ou publica
#          por etapa (§B29), ou a medicao sai desta maquina (questao 2 do §10
#          do HANDOFF — Kafka, RabbitMQ, Redis e Docker estao no ar).
#
# §B27 e o irmao disto e ja esta medido: a ORDEM entre configuracoes decide o
# sinal. O que falta, e e o que esta aqui, e a mesma pergunta DENTRO de uma
# configuracao so — onde nao ha ordem para rotacionar, porque nao ha o que
# alternar.
#
# V15: nenhuma afirmacao de speedup sai daqui. O que sai e a decisao de recorte
# do S08.
#
set -u
cd "$(dirname "$0")/.." || exit 1

REP=${REP:-15}                   # impar, para a mediana cair num valor medido
K=${K:-10}                       # 10 -> 2000x2000
THREADS=${THREADS:-"1 8"}        # 1 e a regua (codigo identico); 8 e onde a
                                 # tabela do S08 vai viver
LADRILHADO=${LADRILHADO:-data/demo$((200 * K))}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "== sondagem do S08: de onde vem o ruido do total do ano? =="
echo "   $REP repeticoes da MESMA configuracao, clock_gettime(CLOCK_MONOTONIC)"
echo "   grade ladrilhada, 5 anos, --variant A. I/O fora da conta (V11)."

# --- a entrada honesta (§B20/§B25: fracao urbana real em grade grande) -------
if [ ! -f "$LADRILHADO/z.txt" ]; then
  echo
  echo "-- ladrilhando o demo200 (nao existia) --"
  ./harness/mktiled.sh "$K" "$LADRILHADO" || exit 1
fi

gcc -std=c11 -Wall -Wextra -O2 -pthread -UNDEBUG -DPROBE_PHASE_TIMING \
    -I. ./*.c -o "$TMP/probe" -lm 2>"$TMP/b.err" || {
  echo "a compilacao instrumentada falhou:"; cat "$TMP/b.err"; exit 1; }

# GUARDA. Esta sonda afirma, no cabecalho, que o "total" e a soma dos slots —
# e a afirmacao e o que torna a pergunta respondivel. Se spread.c passar a
# cronometrar o total por fora, a leitura inteira muda e o script tem de ser
# reescrito, nao remendado. §B28: afirmacao nao conferida e indistinguivel de
# afirmacao falsa.
grep -q 'for (i = 0; i < PROBE_SLOTS; i++) total += probe_acc\[i\];' spread.c || {
  echo
  echo "!! o 'total' da sonda nao e mais a soma dos slots."
  echo "   Esta sondagem parte disso (veja o cabecalho). Releia spread.c antes"
  echo "   de confiar em qualquer numero abaixo."
  exit 1; }

SLOTS="pool|zera delta|fase 1n3|fase 4|fase 5|filtros|merge|total"

etapa ()   { awk -v r="$2" '$1 == r || ($1 " " $2) == r { for (i = 1; i <= NF; i++)
               if ($i == "ms") { print $(i-1); exit } }' "$1"; }
mediana () { printf '%s\n' "$@" | sort -n | awk '{v[NR]=$1} END {print v[int((NR+1)/2)]}'; }

# ---------------------------------------------------------------------------
for n in $THREADS; do
  echo
  echo "############ $n thread(s) ############"

  # colhe REP execucoes inteiras, uma linha por repeticao, um arquivo por slot
  : > "$TMP/ordem.txt"
  rm -f "$TMP"/s_*.txt
  for rep in $(seq 1 "$REP"); do
    "$TMP/probe" --load "$LADRILHADO" --years 5 --seed 1 \
                 --coeff 2,5,20,51,5 --critical-slope 15.0 \
                 --threads "$n" --variant A >/dev/null 2>"$TMP/r.txt"
    i=0
    echo "$SLOTS" | tr '|' '\n' | while IFS= read -r s; do
      i=$((i + 1))
      etapa "$TMP/r.txt" "$s" >> "$TMP/s_$i.txt"
    done
    etapa "$TMP/r.txt" total >> "$TMP/ordem.txt"
  done

  printf '%-12s %8s %8s %8s %9s %8s %9s\n' \
    "etapa" "min" "mediana" "max" "espalh." "cv" "1a/2a met."
  echo "---------------------------------------------------------------------"

  i=0
  echo "$SLOTS" | tr '|' '\n' | while IFS= read -r s; do
    i=$((i + 1))
    awk -v rot="$s" '
      { v[NR] = $1; soma += $1 }
      END {
        if (NR == 0) { printf "%-12s  (slot ausente)\n", rot; exit }
        n = NR
        # mediana, min, max
        for (a = 1; a <= n; a++) for (b = a + 1; b <= n; b++)
          if (v[b] < v[a]) { t = v[a]; v[a] = v[b]; v[b] = t }
        med = v[int((n + 1) / 2)]
        mu  = soma / n
        for (a = 1; a <= n; a++) sq += (v[a] - mu) * (v[a] - mu)
        cv = (mu > 0) ? 100 * sqrt (sq / n) / mu : 0
        esp = (med > 0) ? 100 * (v[n] - v[1]) / med : 0
        printf "%-12s %8.2f %8.2f %8.2f %8.1f%% %7.1f%%", rot, v[1], med, v[n], esp, cv
      }' "$TMP/s_$i.txt"

    # deriva: mediana da 1a metade contra a da 2a, NA ORDEM DE EXECUCAO.
    # O arquivo acima foi ordenado para a mediana; este le o mesmo dado cru.
    awk -v n="$REP" '
      { v[NR] = $1 }
      END {
        if (NR < 4) { printf "      --\n"; exit }
        h = int (NR / 2)
        for (a = 1;     a <= h;  a++) p[a]       = v[a]
        for (a = NR-h+1; a <= NR; a++) q[a-NR+h] = v[a]
        for (a = 1; a <= h; a++) for (b = a+1; b <= h; b++) {
          if (p[b] < p[a]) { t = p[a]; p[a] = p[b]; p[b] = t }
          if (q[b] < q[a]) { t = q[a]; q[a] = q[b]; q[b] = t } }
        m1 = p[int((h+1)/2)]; m2 = q[int((h+1)/2)]
        printf "   %+7.1f%%\n", (m1 > 0) ? 100 * (m2 - m1) / m1 : 0
      }' "$TMP/s_$i.txt"
  done

  echo
  echo "  total do ano, na ordem de execucao (ms):"
  tr '\n' ' ' < "$TMP/ordem.txt" | fold -s -w 68 | sed 's/^/    /'
  cp "$TMP/ordem.txt" "$TMP/ordem_$n.txt"
done

# ---------------------------------------------------------------------------
# 2. qual estimador? — e esta secao existe por causa da FORMA da distribuicao
# ---------------------------------------------------------------------------
# A secao 1 mostra um PISO com picos para cima, nao um ruido simetrico em torno
# de um valor central. Isso tem consequencia direta, e ela nao e sobre quantas
# repeticoes fazer:
#
#   Se a contaminacao e unilateral — carga de fundo rouba tempo, nunca devolve
#   — entao o MINIMO e a estimativa do custo do codigo, e a mediana e uma
#   estimativa do custo do codigo MAIS a carga tipica. A mediana so seria o
#   estimador certo se o desvio fosse para os dois lados.
#
# Isto e testavel com o dado que a secao 1 ja colheu: parte-se a serie em
# grupos de K e pergunta-se qual estimador REPRODUZ entre grupos. O estimador
# bom e o que concorda consigo mesmo; o ruim espalha. Nenhuma teoria sobre a
# causa e necessaria para escolher.
#
# §B25 e o motivo de isto ser medido em vez de argumentado: o projeto ja gastou
# uma sessao atribuindo a cache um efeito que era carga de fundo.
GRUPO=${GRUPO:-5}

echo
echo "############ 2. qual estimador reproduz? ############"
echo "  A serie de $REP e partida em grupos de $GRUPO, na ordem de execucao, e"
echo "  cada estimador e calculado por grupo. Espalhamento MENOR entre grupos"
echo "  = estimador mais reproduzivel. Esta e a escolha que o S08 precisa."
echo
printf '%-10s %10s %10s %10s %10s\n' "threads" "estimador" "por grupo" "espalh." "vale?"
echo "----------------------------------------------------------------"
for n in $THREADS; do
  for est in min mediana media; do
    awk -v g="$GRUPO" -v est="$est" -v n="$n" '
      { v[NR] = $1 }
      END {
        ng = int (NR / g)
        if (ng < 2) { printf "%-10s %10s   grupos de menos (aumente REP)\n", n, est; exit }
        for (k = 0; k < ng; k++) {
          delete w; soma = 0
          for (a = 1; a <= g; a++) { w[a] = v[k*g + a]; soma += w[a] }
          for (a = 1; a <= g; a++) for (b = a+1; b <= g; b++)
            if (w[b] < w[a]) { t = w[a]; w[a] = w[b]; w[b] = t }
          if      (est == "min")     e[k] = w[1]
          else if (est == "mediana") e[k] = w[int((g+1)/2)]
          else                       e[k] = soma / g
          if (k == 0 || e[k] < lo) lo = e[k]
          if (k == 0 || e[k] > hi) hi = e[k]
          lista = lista sprintf ("%.1f ", e[k])
        }
        esp = 100 * (hi - lo) / lo
        printf "%-10s %10s   %-22s %6.1f%%   %s\n", n, est, lista, esp,
               (esp < 5) ? "sim" : ((esp < 10) ? "limitrofe" : "NAO")
        lista = ""
      }' "$TMP/ordem_$n.txt"
  done
  echo "----------------------------------------------------------------"
done

cat <<'FIM'

-- como ler --

A coluna que decide o recorte do S08 e a ULTIMA, "1a/2a met." — a mediana da
segunda metade das repeticoes contra a da primeira, na ordem em que rodaram.

  . perto de zero, com "espalh." e "cv" nao nulos
      -> o ruido e ALEATORIO. Mais repeticao o reduz (cai com a raiz de N), e
         o caminho do S08 e T8: --repeat N cronometrando dentro do processo,
         que e o que V11 ja pede e o S03 adiou. Com o instrumento consertado,
         a tabela de §T do mestre e publicavel como esta recortada.

  . sistematicamente POSITIVA (a segunda metade mais lenta)
      -> e DERIVA, nao ruido. Repetir nao resolve: --repeat so move a deriva
         para dentro do processo, e a media de uma serie que sobe e uma media
         de nada. Ai o S08 muda de forma — publica por etapa (§B29) ou troca
         de maquina (questao 2 do §10 do HANDOFF). NAO escreva o spec do S08
         pedindo tabela de ano inteiro antes de resolver isto.

  . positiva MAS com a ultima execucao no piso
      -> nao e deriva, e CONTAMINACAO UNILATERAL: um piso com picos para cima,
         e os picos calharam de cair na segunda metade. Confira sempre a
         sequencia crua impressa acima antes de ler a coluna — a estatistica
         de duas metades nao distingue "subiu" de "teve picos no fim", e a
         diferenca entre as duas decide o estagio. Deriva termina alto;
         contaminacao volta ao piso.
         Neste caso a pergunta deixa de ser "quantas repeticoes" e passa a ser
         "qual estimador", que e a secao 2.

A outra coluna que importa e "cv", por slot. O total e a SOMA dos sete
(spread.c:111), entao ele nao pode ser mais ruidoso que todos eles: se o total
espalhar mais que a fase 4, ha um slot espalhando mais ainda, e ele esta na
tabela. Ache-o pelo nome antes de teorizar sobre a causa — este projeto ja
gastou uma sessao inteira atribuindo a cache um efeito que era carga de fundo
(§B23, retirado por §B25).

A linha de 1 thread e a regua: nao ha thread, barreira nem trava, entao e o
serial de antes mais a conta da faixa, e todo numero dali e ruido por
construcao. Se a 1 thread ja der espalhamento grande, o problema nao e
paralelismo e nenhuma mudanca em par.c vai melhora-lo.
FIM
