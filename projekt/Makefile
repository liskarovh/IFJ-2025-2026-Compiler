# ===== toolchain & flags =====
CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Werror -pedantic -g
LDFLAGS =

# ===== project layout =====
PROJECT_NAME = compiler
SRC_DIR      = .
BUILD_DIR    = build
OBJ_DIR      = $(BUILD_DIR)/obj

# ===== sources/objects for main build =====
SOURCES  = $(wildcard $(SRC_DIR)/*.c)
OBJECTS  = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# ===== default targets =====
.PHONY: all clean rebuild run \
        lex-test lex-dump \
        test-stack test-symtable test-scopes test-integration tests \
        build-sem-tests test-sem sem sem-tests semantic-tests \
        check-sem-files print-sem-dir

all: $(BUILD_DIR)/$(PROJECT_NAME)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(PROJECT_NAME): $(OBJECTS) | $(BUILD_DIR)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

clean:
	rm -rf $(BUILD_DIR)

rebuild: clean all

run: $(BUILD_DIR)/$(PROJECT_NAME)
	./$(BUILD_DIR)/$(PROJECT_NAME)

# =================================================================
#                       LEX TESTS (scan_dump)
# =================================================================
# testy máš o úroveň výš:
LEX_TEST_DIR := ../test/lex/other

$(OBJ_DIR)/scan_dump.o: $(LEX_TEST_DIR)/scan_dump.c scanner.h token.h error.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/scan_dump: $(OBJ_DIR)/scan_dump.o $(OBJ_DIR)/scanner.o $(OBJ_DIR)/token.o $(OBJ_DIR)/string.o $(OBJ_DIR)/error.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

lex-test: lex-dump

lex-dump: $(BUILD_DIR)/scan_dump
	@if [ -z "$(FILE)" ]; then \
		echo 'Usage: make lex-dump FILE=../test/lex/ok/ids_basic.wren'; \
	else \
		echo '>>> Running scan_dump on $(FILE)'; \
		./$(BUILD_DIR)/scan_dump $(FILE); \
	fi

# =================================================================
#          SEMANTIC UNIT & INTEGRATION TESTS (stack/symtable/scopes)
# =================================================================
# testy máš v ../test/sem
SEM_TESTS_DIR := ../test/sem

# drobný debug target
print-sem-dir:
	@echo "SEM_TESTS_DIR = $(SEM_TESTS_DIR)"
	@ls -l $(SEM_TESTS_DIR) || true

# pre-flight kontrola
check-sem-files:
	@missing=0; \
	for f in $(SEM_TESTS_DIR)/test_stack.c \
	         $(SEM_TESTS_DIR)/test_symtable.c \
	         $(SEM_TESTS_DIR)/test_scope_stack.c \
	         $(SEM_TESTS_DIR)/test_integration_fill_dump.c; do \
	  if [ ! -f $$f ]; then echo "ERR: missing $$f"; missing=1; fi; \
	done; \
	if [ $$missing -ne 0 ]; then exit 2; fi

# --- build: jednotlivé test binárky --------------------------------
$(BUILD_DIR)/test_stack: stack.c $(SEM_TESTS_DIR)/test_stack.c | $(BUILD_DIR) check-sem-files
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_symtable: symtable.c $(SEM_TESTS_DIR)/test_symtable.c | $(BUILD_DIR) check-sem-files
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_scopes: stack.c symtable.c scope_stack.c $(SEM_TESTS_DIR)/test_scope_stack.c | $(BUILD_DIR) check-sem-files
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/test_integration: stack.c symtable.c scope_stack.c $(SEM_TESTS_DIR)/test_integration_fill_dump.c | $(BUILD_DIR) check-sem-files
	$(CC) $(CFLAGS) $^ -o $@

# --- run: pohodlné spouštěcí aliasy --------------------------------
test-stack: $(BUILD_DIR)/test_stack
	@echo '>>> Running stack unit tests'
	@./$(BUILD_DIR)/test_stack

test-symtable: $(BUILD_DIR)/test_symtable
	@echo '>>> Running symtable unit tests (bulk fill + dump)'
	@./$(BUILD_DIR)/test_symtable

test-scopes: $(BUILD_DIR)/test_scopes
	@echo '>>> Running scope_stack unit tests (shadowing, redeclare, cleanup)'
	@./$(BUILD_DIR)/test_scopes

test-integration: $(BUILD_DIR)/test_integration
	@echo '>>> Running integration test (stack + scope_stack + symtable)'
	@./$(BUILD_DIR)/test_integration

# Spustí všechny výše
tests: test-stack test-symtable test-scopes test-integration

# =================================================================
#          SEMANTIC TEST SUITE (symtable/scopes/integration)
# =================================================================
build-sem-tests: $(BUILD_DIR)/test_symtable $(BUILD_DIR)/test_scopes $(BUILD_DIR)/test_integration

# aliasy: 'make sem' nebo 'make test-sem' spustí všechny sémantické testy
test-sem sem sem-tests semantic-tests: test-symtable test-scopes test-integration
	@echo '>>> All semantic tests finished.'
