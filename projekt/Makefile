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
.PHONY: all clean rebuild run lex-test

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
#                       TEST
# =================================================================
TEST_DIR := ../test/lex/other



# ---- scan_dump (plný výpis) -----------------------------------

# Překlad – explicitní cesta do ../test/lex/other
$(OBJ_DIR)/scan_dump.o: ../test/lex/other/scan_dump.c scanner.h token.h error.h | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Link – proti scanner.o, token.o, string.o, error.o
$(BUILD_DIR)/scan_dump: $(OBJ_DIR)/scan_dump.o $(OBJ_DIR)/scanner.o $(OBJ_DIR)/token.o $(OBJ_DIR)/string.o $(OBJ_DIR)/error.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@

.PHONY: lex-dump
lex-dump: $(BUILD_DIR)/scan_dump
	@if [ -z "$(FILE)" ]; then \
		echo 'Usage: make lex-dump FILE=../test/lex/ok/ids_basic.wren'; \
	else \
		echo '>>> Running scan_dump on $(FILE)'; \
		./$(BUILD_DIR)/scan_dump $(FILE); \
	fi

