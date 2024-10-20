CC = clang
CFLAGS = -Wall -Wextra
DEBUG_CFLAGS = -g -O0
RELEASE_CFLAGS = -O2

LFLAGS =

config ?= debug

ifeq ($(config), debug)
	CFLAGS += $(DEBUG_CFLAGS)
else
	CFLAGS += $(RELEASE_CFLAGS)
endif

SRC_DIR = .
BASE_OBJ_DIR = bin-int
BASE_BIN_DIR = bin

OBJ_DIR = $(BASE_OBJ_DIR)/$(config)
BIN_DIR = $(BASE_BIN_DIR)/$(config)

SRC_FILES = $(wildcard $(SRC_DIR)/*.c)
OBJ_FILES = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC_FILES))

BIN = $(BIN_DIR)/bvi

all: $(BIN)

$(BIN): $(OBJ_FILES)
	@mkdir -p $(BIN_DIR)
	$(CC) $(LFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BASE_OBJ_DIR) $(BASE_BIN_DIR)

.PHONY: all clean
