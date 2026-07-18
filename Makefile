# Compiler and flags
CC = gcc
CFLAGS = -Iinclude

# Automatically find all .c files in src/ and utils/
SRC = $(wildcard src/*.c utils/*.c)
OBJ = $(patsubst %.c, %.o, $(SRC))

# Output binary
TARGET = main

# Default target
all: $(TARGET)

# Link object files into final executable
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lws2_32

# Compile each .c into .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Utility targets
clean:
	rm -f $(OBJ) $(TARGET)

run: $(TARGET)
	./$(TARGET)