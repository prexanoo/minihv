CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Wextra -Werror -pedantic -O2
CPPFLAGS := -Isrc

BIN   := minihv
CONF  ?= conf/hv.conf
SRC   := $(wildcard src/*/*.c)
OBJ   := $(patsubst src/%.c,build/%.o,$(SRC))
MAIN  := build/main.o
TESTS := $(patsubst tests/%.c,build/tests/%,$(wildcard tests/test_*.c))

VG := valgrind --leak-check=full --errors-for-leak-kinds=definite --error-exitcode=1 -q

all: $(BIN)

$(BIN): $(OBJ) $(MAIN)
	$(CC) $(CFLAGS) -o $@ $^

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build/tests/%: tests/%.c $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^

out:
	@mkdir -p out

run: $(BIN) | out
	./$(BIN) -c $(CONF)

test: $(TESTS) | out
	@for t in $(TESTS); do echo "== $$t"; ./$$t || exit 1; done
	@echo "todos os testes passaram"

# roda o binario em todos os cenarios de conf/ e todos os testes no valgrind
valgrind: $(BIN) $(TESTS) | out
	@for c in conf/*.conf; do \
		echo "== $$c"; $(VG) ./$(BIN) -c $$c > /dev/null || exit 1; done
	@for t in $(TESTS); do \
		echo "== $$t"; $(VG) ./$$t > /dev/null || exit 1; done
	@echo "valgrind ok"

clean:
	rm -rf build $(BIN) out

-include $(OBJ:.o=.d) $(MAIN:.o=.d)

.PHONY: all run test valgrind clean
