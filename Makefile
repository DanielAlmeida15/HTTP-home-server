.PHONY: all clean

CC = gcc
CFLAGS = -Wall -Iinclude

SRC_DIR = src
BIN_DIR = bin

SRCS = $(wildcard $(SRC_DIR/*.c))
OBJS = $(SRCS:$(SRC_DIR/%.c=$(BIN_DIR/%.o)))
TARGET = $(BIN_DIR)/server

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)
	rm -rf $(BIN_DIR)/*.o

$(BIN_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BIN_DIR)/*.o $(TARGET)
