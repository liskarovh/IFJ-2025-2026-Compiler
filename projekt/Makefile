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
TEST_DIR := ../test

# Objecty testů
$(OBJ_DIR)/scan_driver.o: $(TEST_DIR)/scan_driver.c scanner.h token.h
	$(CC) $(CFLAGS) -c $(TEST_DIR)/scan_driver.c -o $@

$(OBJ_DIR)/scan_append.o: $(TEST_DIR)/scan_append.c scanner.h token.h
	$(CC) $(CFLAGS) -c $(TEST_DIR)/scan_append.c -o $@

# Link
$(BUILD_DIR)/scan_driver: $(OBJ_DIR)/scanner.o $(OBJ_DIR)/token.o $(OBJ_DIR)/string.o $(OBJ_DIR)/error.o $(OBJ_DIR)/scan_driver.o
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/scan_append: $(OBJ_DIR)/scanner.o $(OBJ_DIR)/token.o $(OBJ_DIR)/string.o $(OBJ_DIR)/error.o $(OBJ_DIR)/scan_append.o
	$(CC) $(CFLAGS) $^ -o $@

# Spuštění (stdin)
lex-driver: $(BUILD_DIR)/scan_driver
	@echo '>>> scan_driver: reading from stdin (Ctrl-D to end)'
	./$(BUILD_DIR)/scan_driver

lex-append: $(BUILD_DIR)/scan_append
	@echo '>>> scan_append: reading from stdin (Ctrl-D to end)'
	./$(BUILD_DIR)/scan_append

# ---- scan_dump (plný výpis)
build/obj/scan_dump.o: ../test/scan_dump.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c ../test/scan_dump.c -o build/obj/scan_dump.o

build/scan_dump: build/obj/scan_dump.o build/obj/scanner.o build/obj/token.o build/obj/string.o build/obj/error.o | $(BUILD_DIR)
	$(CC) $(CFLAGS) build/obj/scan_dump.o build/obj/scanner.o build/obj/token.o build/obj/string.o build/obj/error.o -o build/scan_dump

.PHONY: lex-dump
lex-dump: build/scan_dump
	@if [ -z "$(FILE)" ]; then \
		echo 'Usage: make lex-dump FILE=../test/sample.txt'; \
	else \
		echo '>>> Running scan_dump on $(FILE)'; \
		./build/scan_dump $(FILE); \
	fi
