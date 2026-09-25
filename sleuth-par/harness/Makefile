#
# harness/ — ferramentas de teste do estagio S03.
#
# NADA AQUI E LINKADO AO BINARIO sleuth-par. A restricao §C do spec mestre
# tira GIF e o subsistema GD do prototipo; estas sao ferramentas de
# preparacao de dados e de construcao do baseline de referencia.
#
# Alvos:
#   make gif2asc     conversor GIF -> texto (linka so o gd.c do SLEUTH)
#   make data        gera ../data/demo200/*.txt  (precisa de gif2asc)
#   make baseline    constroi o `grow` de referencia numa copia de SLEUTH/
#   make scenario    gera o cenario de referencia de V7
#   make verify      confere que o gif2asc reproduz os numeros de §D1 (VS8)
#   make smoke       roda o baseline 1990->1995 e mostra o avg.log
#   make clean       remove build/
#   make distclean   remove build/ e os dados gerados
#
# A arvore SLEUTH/ e somente-leitura (§R2). Tudo o que precisa ser compilado
# ou corrigido acontece sobre uma copia em build/.
#

SHELL     = /bin/bash
TCC       = ../..
SLEUTH    = $(TCC)/SLEUTH
BUILD     = build
REF       = $(BUILD)/sleuth-ref
DATA      = ../data/demo200
SCENARIO  = $(BUILD)/scenario.ref
REFOUT    = $(BUILD)/refout

# O gd.c e de 1994: K&R tardio, sem prototipos completos, e inclui mtables.c
# por #include em vez de compilar separado. -std=gnu89 -w e o que faz passar.
GDSRC     = gd.c gd.h mtables.c
GDFLAGS   = -std=gnu89 -w -O1

.PHONY: all gif2asc data baseline scenario verify smoke clean distclean

all: gif2asc

# ---------------------------------------------------------------- gif2asc ---
gif2asc: $(BUILD)/gif2asc

$(BUILD)/gif2asc: gif2asc.c
	@mkdir -p $(BUILD)/gd
	@for f in $(GDSRC); do cp $(SLEUTH)/GD/$$f $(BUILD)/gd/; done
	$(CC) $(GDFLAGS) -I$(BUILD)/gd -o $@ gif2asc.c $(BUILD)/gd/gd.c -lm
	@echo "  gif2asc pronto"

# ------------------------------------------------------------------- data ---
data: $(BUILD)/gif2asc
	./mkdata.sh $(DATA)

# --------------------------------------------------------------- baseline ---
#
# O `grow` que vem na arvore NAO serve: e um ELF MIPS N32 de 2001 (e o
# grow.exe e de 2005). Precisa ser construido. E, construido cru, aborta —
# ver o patch de date_str. Ver §B1 e §B6 de specs/S03-harness.md.
#
baseline: $(REF)/grow

$(REF)/grow: 0001-date_str-overflow.patch
	@echo "== construindo o grow de referencia =="
	@rm -rf $(REF)
	@mkdir -p $(BUILD)
	@cp -r $(SLEUTH) $(REF)
	@rm -f $(REF)/grow $(REF)/grow.exe        # binarios MIPS/Windows do tarball
	@echo "-- aplicando $<"
	@patch -d $(REF) -p1 --no-backup-if-mismatch < $<
	@echo "-- make GD"
	@$(MAKE) -C $(REF)/GD >$(BUILD)/build-gd.log 2>&1 \
	  || { echo "FALHOU: veja $(BUILD)/build-gd.log"; tail -20 $(BUILD)/build-gd.log; exit 1; }
	@echo "-- make grow"
	@$(MAKE) -C $(REF) >$(BUILD)/build-grow.log 2>&1 \
	  || { echo "FALHOU: veja $(BUILD)/build-grow.log"; tail -20 $(BUILD)/build-grow.log; exit 1; }
	@file $(REF)/grow | sed 's/^/  /'

# --------------------------------------------------------------- scenario ---
scenario: $(SCENARIO)

$(SCENARIO): mkscenario.sh
	@mkdir -p $(BUILD)
	@./mkscenario.sh $(SCENARIO) $(abspath $(REFOUT)) 2010

# ----------------------------------------------------------------- verify ---
#
# VS8: o gif2asc tem de reproduzir exatamente as contagens levantadas em §D1
# do spec. Se esta checagem quebrar, ou o GD mudou de comportamento ou os
# dados de entrada nao sao os que o spec descreve.
#
verify: $(BUILD)/gif2asc
	@echo "== VS8: histogramas do demo200 (comparar com §D1) =="
	@for f in urban.1930 urban.1950 urban.1970 urban.1990 \
	          roads.1930 roads.1950 roads.1970 roads.1990; do \
	  $(BUILD)/gif2asc $(SLEUTH)/Input/demo200/demo200.$$f.gif --stats \
	    | sed 's|.*/demo200\.||'; \
	done
	@for f in slope excluded; do \
	  $(BUILD)/gif2asc $(SLEUTH)/Input/demo200/demo200.$$f.gif --stats \
	    | sed 's|.*/demo200\.||' | cut -c1-118; \
	done
	@echo
	@echo "esperado (§D1): urban 598/861/1821/3881 · roads 375/665/945/1104"
	@echo "                slope max=41 · excluded 35775 em 1 e 4225 em 100"

# ------------------------------------------------------------------ smoke ---
#
# Prova de fumaca do baseline: 1990->1995, rapido, so para confirmar que o
# encadeamento copia -> patch -> build -> cenario -> execucao fecha.
#
smoke: baseline $(BUILD)/gif2asc
	@./mkscenario.sh $(BUILD)/scenario.smoke $(abspath $(BUILD)/smokeout) 1995
	@rm -rf $(BUILD)/smokeout && mkdir -p $(BUILD)/smokeout
	@./grow-ref.sh predict $(abspath $(BUILD)/scenario.smoke) >$(BUILD)/smoke.out 2>&1 \
	  && echo "  grow: exit 0" \
	  || { echo "  grow FALHOU:"; tail -12 $(BUILD)/smoke.out; exit 1; }
	@echo
	@echo "== avg.log =="
	@cat $(BUILD)/smokeout/avg.log
	@echo
	@echo "VS10: as colunas de coeficientes (diffus spread breed slp_res rd_grav)"
	@echo "      devem ser constantes — automodificacao neutralizada:"
	@awk 'NR > 1 { printf "  %s  diffus=%s spread=%s breed=%s slp_res=%s rd_grav=%s\n", \
	                      $$2, $$18, $$19, $$20, $$21, $$22 }' \
	    $(BUILD)/smokeout/avg.log

clean:
	rm -rf $(BUILD)

distclean: clean
	rm -rf $(DATA)
