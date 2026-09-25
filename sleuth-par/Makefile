#
# sleuth-par — nucleo do SLEUTH com threads POSIX
#
# Alvos:
#   make          binario otimizado, asserts ligados
#   make asan     reconstroi com AddressSanitizer + UBSan
#   make tsan     reconstroi com ThreadSanitizer (VS23, estagio S05)
#   make memcheck roda 5 anos 20x20 sob valgrind (VS2)
#   make check    suite de verificacao do estagio (tests/run_checks.sh)
#   make v7       compara o prototipo serial com o SLEUTH original (VS11)
#   make clean
#
# -UNDEBUG e deliberado: o spread.c original usa assert() para checar
# dimensoes de grade, e queremos esses asserts ativos ate S08.
#

CC       = gcc
CSTD     = -std=c11
WARN     = -Wall -Wextra
OPT      = -O2
CFLAGS   = $(CSTD) $(WARN) $(OPT) -g -pthread -UNDEBUG
LDFLAGS  = -pthread
LDLIBS   = -lm

BIN      = sleuth-par
SRCS     = $(wildcard *.c)
OBJS     = $(SRCS:.c=.o)
DEPS     = $(SRCS:.c=.d)

.PHONY: all asan tsan memcheck check v7 rngtest clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Sanitizers: complementam o valgrind, nao o substituem. ASan pega
# estouro em array global (que o valgrind nao ve) e roda bem mais rapido.
asan: CFLAGS  += -fsanitize=address,undefined -fno-omit-frame-pointer -O1
asan: LDFLAGS += -fsanitize=address,undefined
asan: clean $(BIN)

# ThreadSanitizer — VS23 do estagio S05.
#
# NAO e redundante com a invariante V3, e a diferenca e a razao de VS23
# existir: V3 diz "o resultado e o mesmo", TSan diz "o programa e bem
# definido". Sao perguntas diferentes e as duas precisam de resposta.
#
# O caso concreto esta em §D6 do spec do estagio: duas threads gravando
# PHASE4G na mesma celula produzem saida IDENTICA — todas as escritas da fase
# 4 carregam a mesma etiqueta — e continuam sendo corrida de dados, isto e,
# comportamento indefinido. Medido neste estagio: com --variant none e 8
# threads, 6 de 6 execucoes no demo200 deram o `z` certo. Passar em V3 e
# reprovar em TSan e exatamente o cenario a temer, porque e o que sobrevive a
# uma bateria de testes e falha num compilador diferente.
#
# Incompativel com ASan na mesma compilacao — por isso sao dois alvos.
tsan: CFLAGS  += -fsanitize=thread -fno-omit-frame-pointer -O1
tsan: LDFLAGS += -fsanitize=thread
tsan: clean $(BIN)

# VS2 do estagio S02
memcheck: $(BIN)
	valgrind --error-exitcode=1 --leak-check=full --track-origins=yes \
	         ./$(BIN) --rows 20 --cols 20 --years 5 --seed 1

check: $(BIN)
	./tests/run_checks.sh

# VS11 do estagio S03: V7, o prototipo serial contra o `grow` original.
# Constroi o baseline de referencia se ainda nao existir.
v7: $(BIN)
	./tests/compare_v7.sh

# VS15 e VS16 do estagio S04: o gerador ancorado, testado em isolamento.
#
# Liga contra todos os objetos menos main.o — rng.c precisa de random.o (o
# ran1, para o modo legacy) e stubs.o (proc_GetCurrentYear, para o ano da
# chave), e esses arrastam o resto. Nao vale a pena recortar: sao 8 objetos.
TESTOBJS = $(filter-out main.o,$(OBJS))

tests/rng_test: tests/rng_test.c $(TESTOBJS)
	$(CC) $(CFLAGS) -I. $< $(TESTOBJS) -o $@ $(LDLIBS)

rngtest: tests/rng_test
	./tests/rng_test

clean:
	rm -f $(OBJS) $(DEPS) $(BIN) tests/rng_test

-include $(DEPS)
