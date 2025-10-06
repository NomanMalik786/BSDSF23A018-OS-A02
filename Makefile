CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
TARGET = $(BIN_DIR)/ls

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

all: prepare $(TARGET)

prepare:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR) $(MAN_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	-rm -f $(OBJ_DIR)/*.o

distclean: clean
	-rm -f $(TARGET)

.PHONY: all prepare clean distclean
