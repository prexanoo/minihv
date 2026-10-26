CC     = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -pedantic -O2 -Isrc

BIN   = minihv
CONF  = conf/hv.conf
SRC   = $(wildcard src/*/*.c)
HDR   = $(wildcard src/*/*.h)
OBJ   = $(patsubst src/%.c,build/%.o,$(SRC))
TESTS = $(patsubst tests/%.c,build/tests/%,$(wildcard tests/test_*.c))

VG = valgrind --leak-check=full --errors-for-leak-kinds=definite --error-exitcode=1 -q

all: $(BIN)

$(BIN): $(OBJ) build/main.o
	$(CC) $(CFLAGS) -o $@ $^

# cada .o depende de todos os headers: mais simples e nunca fica desatualizado
build/%.o: src/%.c $(HDR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/tests/%: tests/%.c $(OBJ)
	mkdir -p build/tests
	$(CC) $(CFLAGS) -o $@ $^

run: $(BIN)
	mkdir -p out
	./$(BIN) -c $(CONF)

test: $(TESTS)
	mkdir -p out
	for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done
	@echo "todos os testes passaram"

# roda o binario em todos os cenarios de conf/ e todos os testes no valgrind
valgrind: $(BIN) $(TESTS)
	mkdir -p out
	for c in conf/*.conf; do echo "== $$c"; $(VG) ./$(BIN) -c $$c > /dev/null || exit 1; done
	for t in $(TESTS); do echo "== $$t"; $(VG) ./$$t > /dev/null || exit 1; done
	@echo "valgrind ok"

clean:
	rm -rf build $(BIN) out

.PHONY: all run test valgrind clean
