# ===== toolchain & flags =====
CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Werror -pedantic -g
LDFLAGS =

# ===== project layout =====
PROJECT_NAME = compiler
SRC_DIR      = .

# ===== sources/objects for main build =====
SOURCES  = $(wildcard $(SRC_DIR)/*.c)
OBJECTS  = $(SOURCES:$(SRC_DIR)/%.c=%.o)

# ===== default targets =====
.PHONY: all clean clean-objects rebuild run \
        lex-test lex-dump \
        test-stack test-symtable test-scopes test-integration tests \
        build-sem-tests test-sem sem sem-tests semantic-tests \
        check-sem-files print-sem-dir

all: $(PROJECT_NAME) clean-objects

%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(PROJECT_NAME): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

clean-objects:
	rm -f *.o

clean:
	rm -f *.o \
	      $(PROJECT_NAME) \
	      scan_dump \
	      test_stack test_symtable test_scopes test_integration

rebuild: clean all

run: $(PROJECT_NAME)
	./$(PROJECT_NAME)

# =================================================================
#                       LEX TESTS (scan_dump)
# =================================================================
LEX_TEST_DIR := ../test/lex/other

scan_dump.o: $(LEX_TEST_DIR)/scan_dump.c scanner.h token.h error.h
	$(CC) $(CFLAGS) -c $< -o $@

scan_dump: scan_dump.o scanner.o token.o string.o error.o
	$(CC) $(CFLAGS) $^ -o $@

lex-test: lex-dump

lex-dump: scan_dump
	@if [ -z "$(FILE)" ]; then \
		echo 'Usage: make lex-dump FILE=../test/lex/ok/ids_basic.wren'; \
	else \
		echo '>>> Running scan_dump on $(FILE)'; \
		./scan_dump $(FILE); \
	fi

# =================================================================
#          SEMANTIC UNIT & INTEGRATION TESTS (stack/symtable/scopes)
# =================================================================
SEM_TESTS_DIR := ../test/sem


print-sem-dir:
	@echo "SEM_TESTS_DIR = $(SEM_TESTS_DIR)"
	@ls -l $(SEM_TESTS_DIR) || true

check-sem-files:
	@missing=0; \
	for f in $(SEM_TESTS_DIR)/test_stack.c \
	         $(SEM_TESTS_DIR)/test_symtable.c \
	         $(SEM_TESTS_DIR)/test_scope_stack.c \
	         $(SEM_TESTS_DIR)/test_integration_fill_dump.c; do \
	  if [ ! -f $$f ]; then echo "ERR: missing $$f"; missing=1; fi; \
	done; \
	if [ $$missing -ne 0 ]; then exit 2; fi

# --- build--------------------------------
test_stack: stack.c $(SEM_TESTS_DIR)/test_stack.c | check-sem-files
	$(CC) $(CFLAGS) $^ -o $@

test_symtable: symtable.c $(SEM_TESTS_DIR)/test_symtable.c | check-sem-files
	$(CC) $(CFLAGS) $^ -o $@

test_scopes: stack.c symtable.c scope_stack.c $(SEM_TESTS_DIR)/test_scope_stack.c | check-sem-files
	$(CC) $(CFLAGS) $^ -o $@

test_integration: stack.c symtable.c scope_stack.c $(SEM_TESTS_DIR)/test_integration_fill_dump.c | check-sem-files
	$(CC) $(CFLAGS) $^ -o $@

# --- run--------------------------------
test-stack: test_stack
	@echo '>>> Running stack unit tests'
	@./test_stack

test-symtable: test_symtable
	@echo '>>> Running symtable unit tests (bulk fill + dump)'
	@./test_symtable

test-scopes: test_scopes
	@echo '>>> Running scope_stack unit tests (shadowing, redeclare, cleanup)'
	@./test_scopes

test-integration: test_integration
	@echo '>>> Running integration test (stack + scope_stack + symtable)'
	@./test_integration

tests: test-stack test-symtable test-scopes test-integration

# =================================================================
#          SEMANTIC TEST SUITE (symtable/scopes/integration)
# =================================================================
build-sem-tests: test_symtable test_scopes test_integration

test-sem sem sem-tests semantic-tests: test-symtable test-scopes test-integration
	@echo '>>> All semantic tests finished.'
